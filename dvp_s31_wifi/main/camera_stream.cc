// GC2145 DVP -> RGB565_BE -> software JPEG -> Wi-Fi browser preview.
#include <algorithm>
#include <cstring>
#include "board_config.h"
#include "camera_power.h"
#include "gc2145_vga_settings.h"
#include "web_preview.h"
#include "driver/i2c_master.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG="s31_camera";
constexpr int kWidth=640, kHeight=480;
constexpr size_t kFrameBytes=kWidth*kHeight*2, kJpegBytes=256*1024;
struct Frame { int index; size_t size; };
uint8_t* frames[2];
QueueHandle_t free_frames, ready_frames;
i2c_master_dev_handle_t sensor;
esp_cam_ctlr_handle_t controller;

esp_err_t Write(uint8_t reg,uint8_t value) {
    uint8_t bytes[]={reg,value};
    const auto err=i2c_master_transmit(sensor,bytes,sizeof(bytes),100);
    if(err!=ESP_OK) ESP_LOGE(TAG,"SCCB write reg=0x%02x value=0x%02x: %s",reg,value,esp_err_to_name(err));
    return err;
}
esp_err_t Read(uint8_t reg,uint8_t* value) {
    const auto err=i2c_master_transmit_receive(sensor,&reg,1,value,1,100);
    ESP_LOGI(TAG,"SCCB read reg=0x%02x value=0x%02x: %s",reg,*value,esp_err_to_name(err));
    return err;
}
bool IRAM_ATTR Next(esp_cam_ctlr_handle_t,esp_cam_ctlr_trans_t* trans,void*) {
    int index;
    BaseType_t wake=pdFALSE;
    if(xQueueReceiveFromISR(free_frames,&index,&wake)==pdTRUE) {
        trans->buffer=frames[index]; trans->buflen=kFrameBytes;
    } // No slot available: driver captures into its private backup buffer.
    return wake==pdTRUE;
}
bool IRAM_ATTR Done(esp_cam_ctlr_handle_t,esp_cam_ctlr_trans_t* trans,void*) {
    BaseType_t wake=pdFALSE;
    for(int i=0;i<2;++i) if(trans->buffer==frames[i]) {
        Frame frame{i,trans->received_size};
        if(xQueueSendFromISR(ready_frames,&frame,&wake)!=pdTRUE)
            xQueueSendFromISR(free_frames,&i,&wake);
        break;
    }
    return wake==pdTRUE;
}
void LogPixels(const uint8_t* data,uint32_t sequence,int jpeg_size,int64_t encode_us) {
    uint32_t sum=0, count=0;
    unsigned minimum=255, maximum=0;
    for(size_t i=0;i<kFrameBytes;i+=32) {
        unsigned rgb=(unsigned(data[i])<<8)|data[i+1];
        unsigned r=((rgb>>11)&31)*255/31, g=((rgb>>5)&63)*255/63, b=(rgb&31)*255/31;
        unsigned luma=(77*r+150*g+29*b+128)>>8;
        minimum=std::min(minimum,luma); maximum=std::max(maximum,luma); sum+=luma; ++count;
    }
    ESP_LOGI(TAG,"frame=%u raw=%u RGB565_BE luma=%u..%u mean=%u JPEG=%d encode=%lldms head=%02x %02x %02x %02x",
             unsigned(sequence),unsigned(kFrameBytes),minimum,maximum,unsigned(sum/count),jpeg_size,
             (long long)(encode_us/1000),data[0],data[1],data[2],data[3]);
}
void EncodeTask(void*) {
    jpeg_enc_config_t config=DEFAULT_JPEG_ENC_CONFIG();
    config.width=kWidth; config.height=kHeight;
    config.src_type=JPEG_PIXEL_FORMAT_RGB565_BE;
    config.subsampling=JPEG_SUBSAMPLE_420; config.quality=70;
    jpeg_enc_handle_t encoder=nullptr;
    if(jpeg_enc_open(&config,&encoder)!=JPEG_ERR_OK) {
        ESP_LOGE(TAG,"JPEG encoder init failed"); vTaskDelete(nullptr); return;
    }
    auto* output=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,kJpegBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!output) { jpeg_enc_close(encoder); ESP_LOGE(TAG,"JPEG output allocation failed"); vTaskDelete(nullptr); return; }
    uint32_t sequence=0;
    int64_t last_log=0;
    for(;;) {
        Frame frame;
        if(xQueueReceive(ready_frames,&frame,pdMS_TO_TICKS(5000))!=pdTRUE) {
            ESP_LOGW(TAG,"no DVP frame for 5s"); continue;
        }
        ++sequence;
        if(sequence>8 && frame.size==kFrameBytes) {
            int size=0;
            int64_t start=esp_timer_get_time();
            auto err=jpeg_enc_process(encoder,frames[frame.index],kFrameBytes,output,kJpegBytes,&size);
            int64_t now=esp_timer_get_time();
            if(err==JPEG_ERR_OK && size>0 && size<=int(kJpegBytes)) {
                web_preview_push_frame(output,size);
                if(now-last_log>=1000000) { LogPixels(frames[frame.index],sequence,size,now-start); last_log=now; }
            } else ESP_LOGE(TAG,"JPEG encode failed: %d size=%d",int(err),size);
        }
        xQueueSend(free_frames,&frame.index,portMAX_DELAY);
        // Encoding can keep the ready queue permanently nonempty. Give IDLE
        // one tick between frames rather than disabling the task watchdog.
        vTaskDelay(1);
    }
}
}

extern "C" void app_main() {
    ESP_LOGI(TAG,"Standalone S31/GC2145: DVP capture, JPEG encoding and Wi-Fi preview");
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port=I2C_NUM_1; bus_config.sda_io_num=CAM_PIN_SCCB_SDA;
    bus_config.scl_io_num=CAM_PIN_SCCB_SCL; bus_config.clk_source=I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt=7; bus_config.flags.enable_internal_pullup=true;
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config,&bus));
    tca9555_t io{}; camera_power_state_t power{};
    ESP_ERROR_CHECK(tca9555_init(&io,bus,CAM_EXPANDER_ADDR));
    ESP_ERROR_CHECK(camera_power_on(&io,&power));
    // One startup reset before camera configuration and streaming.
    ESP_ERROR_CHECK(camera_reset_pulse(&io,&power));

    esp_cam_ctlr_dvp_pin_config_t pins{};
    pins.data_width=CAM_CTLR_DATA_WIDTH_8;
    for(int i=0;i<ESP_CAM_CTLR_DVP_DATA_SIG_NUM;++i) pins.data_io[i]=i<8?gpio_num_t(46+i):GPIO_NUM_NC;
    pins.pclk_io=CAM_PIN_PCLK; pins.vsync_io=CAM_PIN_VSYNC; pins.de_io=CAM_PIN_HSYNC; pins.xclk_io=CAM_PIN_MCLK;
    esp_cam_ctlr_dvp_config_t config{};
    config.clk_src=CAM_CLK_SRC_DEFAULT; config.h_res=kWidth; config.v_res=kHeight;
    config.input_data_color_type=CAM_CTLR_COLOR_RGB565; config.output_data_color_type=CAM_CTLR_COLOR_RGB565;
    config.cam_data_width=8; config.xclk_freq=20000000; config.external_xtal=false;
    config.dma_burst_size=64; config.pin=&pins;
    ESP_ERROR_CHECK(esp_cam_new_dvp_ctlr(&config,&controller));
    ESP_LOGI(TAG,"LCD_CAM MCLK GPIO55 configured 20MHz; waiting 10ms");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(i2c_master_probe(bus,CAM_GC2145_SCCB_ADDR,100));
    i2c_device_config_t device{};
    device.dev_addr_length=I2C_ADDR_BIT_LEN_7; device.device_address=CAM_GC2145_SCCB_ADDR; device.scl_speed_hz=100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&device,&sensor));
    ESP_ERROR_CHECK(Write(0xfe,0));
    uint8_t high=0, low=0;
    ESP_ERROR_CHECK(Read(0xf0,&high)); ESP_ERROR_CHECK(Read(0xf1,&low));
    ESP_LOGI(TAG,"PID=0x%04x (expected 0x2145)",(unsigned(high)<<8)|low);
    ESP_ERROR_CHECK((high==0x21 && low==0x45)?ESP_OK:ESP_ERR_NOT_FOUND);
    for(const auto& entry:kGc2145Vga) {
        if(entry.reg==0xff) vTaskDelay(pdMS_TO_TICKS(entry.value));
        else ESP_ERROR_CHECK(Write(entry.reg,entry.value));
    }
    ESP_ERROR_CHECK(Write(0xfe,0)); ESP_ERROR_CHECK(Write(0xf2,0));
    free_frames=xQueueCreate(2,sizeof(int)); ready_frames=xQueueCreate(2,sizeof(Frame));
    ESP_ERROR_CHECK(free_frames && ready_frames?ESP_OK:ESP_ERR_NO_MEM);
    for(int i=0;i<2;++i) {
        frames[i]=static_cast<uint8_t*>(esp_cam_ctlr_alloc_buffer(controller,kFrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA));
        ESP_ERROR_CHECK(frames[i]?ESP_OK:ESP_ERR_NO_MEM);
        xQueueSend(free_frames,&i,portMAX_DELAY);
    }
    ESP_ERROR_CHECK(web_preview_start());
    esp_cam_ctlr_evt_cbs_t callbacks{}; callbacks.on_get_new_trans=Next; callbacks.on_trans_finished=Done;
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(controller,&callbacks,nullptr));
    if(xTaskCreate(EncodeTask,"jpeg_stream",8192,nullptr,5,nullptr)!=pdPASS) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(controller)); ESP_ERROR_CHECK(esp_cam_ctlr_start(controller));
    ESP_ERROR_CHECK(Write(0xf2,0x0f));
}
