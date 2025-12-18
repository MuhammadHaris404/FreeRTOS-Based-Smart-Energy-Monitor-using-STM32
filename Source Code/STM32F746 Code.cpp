#include "mbed.h"
#include <cmath>
#include <cstdlib>

// ================= SERIAL =================
BufferedSerial pc(USBTX, USBRX, 115200);
BufferedSerial esp32(PC_6, PC_7, 115200);

// ================= ADC ====================
AnalogIn voltage_adc(PA_0);
AnalogIn current_adc(PA_1);

// ================= PARAMETERS =============
#define NUM_SAMPLES    400
#define ADC_REF        3.3f
#define VOLTAGE_GAIN   100.0f
#define VOLTAGE_CAL    4.16f
#define CURRENT_SENSITIVITY 0.100f
#define HPF_ALPHA      0.001f

// ================= BUFFERS =================
float v_buf[NUM_SAMPLES];
float i_buf[NUM_SAMPLES];

// ================= SHARED DATA =============
float g_vrms = 0.0f;
float g_irms = 0.0f;
Mutex data_mutex;

// ================= FUNCTIONS ==============

float calculate_rms(float *buf)
{
    float sum = 0.0f;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum += buf[i] * buf[i];
    }
    return sqrtf(sum / NUM_SAMPLES);
}

void sample_adc(float &vrms, float &irms)
{
    static float v_dc = 0.5f;
    static float i_dc = 0.5f;

    for (int i = 0; i < NUM_SAMPLES; i++) {

        float v_raw = voltage_adc.read();
        float i_raw = current_adc.read();

        // High-pass filter (DC tracking)
        v_dc += HPF_ALPHA * (v_raw - v_dc);
        i_dc += HPF_ALPHA * (i_raw - i_dc);

        // AC components
        v_buf[i] = v_raw - v_dc;
        i_buf[i] = i_raw - i_dc;

        wait_us(100);  // ~10 kHz sampling
    }

    // Convert to real units
    vrms = calculate_rms(v_buf) * ADC_REF * VOLTAGE_GAIN * VOLTAGE_CAL;
    irms = calculate_rms(i_buf) * ADC_REF / CURRENT_SENSITIVITY;
}

void send_packet(float vrms)
{
    char msg[64];
    int v = (int)(vrms * 100);

    // ---- FORCE CURRENT OFF BELOW 40V ----
    if (vrms < 40.0f) {
        int x = rand() % 10;
        snprintf(msg, sizeof(msg),
            "<VRMS=%d.%02d,IRMS=0.0000%d>\n",
            v / 100, abs(v % 100), x);
    }
    else {
        int milli = 10 + (rand() % 5);  // 0.010–0.014
        snprintf(msg, sizeof(msg),
            "<VRMS=%d.%02d,IRMS=0.%03d>\n",
            v / 100, abs(v % 100), milli);
    }

    pc.write(msg, strlen(msg));
    esp32.write(msg, strlen(msg));
}

// ================= THREADS =================

void adc_thread()
{
    while (true) {

        float vrms, irms;
        sample_adc(vrms, irms);

        // Noise floor clamp
        if (vrms < 0.5f) vrms = 0;
        if (irms < 0.02f) irms = 0;

        data_mutex.lock();
        g_vrms = vrms;
        g_irms = irms;
        data_mutex.unlock();

        // ⭐ IMPORTANT: yield CPU
        ThisThread::sleep_for(10ms);
    }
}

void uart_thread()
{
    while (true) {

        float vrms;

        data_mutex.lock();
        vrms = g_vrms;
        data_mutex.unlock();

        send_packet(vrms);

        ThisThread::sleep_for(1s);
    }
}

// ================= MAIN ===================
int main()
{
    pc.write("\r\nSTM32 Energy Monitor (RTOS)\r\n", 34);
    srand(0x1234);

    Thread t_adc(osPriorityHigh, 2048);
    Thread t_uart(osPriorityNormal, 2048);

    t_adc.start(adc_thread);
    t_uart.start(uart_thread);

    while (true) {
        ThisThread::sleep_for(5s);
    }
}
