#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

void map_joystick_to_midi(PmStream *midi_stream, unsigned char *buf, size_t len) {
    static int prev_buttons[12] = {0};
    int current_buttons[12] = {
        buf[5] & 0x10, buf[5] & 0x20, buf[5] & 0x40, buf[5] & 0x80,
        buf[6] & 0x01, buf[6] & 0x02, buf[6] & 0x04, buf[6] & 0x08,
        buf[6] & 0x10, buf[6] & 0x20, buf[6] & 0x40, buf[6] & 0x80
    };
    int midi_notes[12] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79};
    
    for (int i = 0; i < 12; i++) {
        if (current_buttons[i] != prev_buttons[i]) {
            send_midi_message(midi_stream, 0x90, midi_notes[i], current_buttons[i] ? 127 : 0);
            prev_buttons[i] = current_buttons[i];
        }
    }
}

int main() {
    int res;
    unsigned char buf[65];
    hid_device *handle;
    PmStream *midi_stream;
    PmError pm_err;
    int num_devices, i;
    const PmDeviceInfo *info;
    int midi_device_id = -1;

    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    handle = hid_open(0x1949, 0x0402, NULL); // Aolion AL-K10
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        return -1;
    }

    pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al inicializar PortMidi: %s\n", Pm_GetErrorText(pm_err));
        hid_close(handle);
        return -1;
    }

    num_devices = Pm_CountDevices();
    for (i = 0; i < num_devices; i++) {
        info = Pm_GetDeviceInfo(i);
        if (info != NULL && info->output) {
            midi_device_id = i;
            printf("Usando dispositivo MIDI: ID %d, Nombre %s, Interfaz %s\n", i, info->name, info->interf);
            break;
        }
    }

    if (midi_device_id == -1) {
        fprintf(stderr, "No se encontraron dispositivos MIDI de salida\n");
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    pm_err = Pm_OpenOutput(&midi_stream, midi_device_id, NULL, 128, NULL, NULL, 0);
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al abrir el dispositivo MIDI: %s\n", Pm_GetErrorText(pm_err));
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            map_joystick_to_midi(midi_stream, buf, res);
        }
        usleep(1000);
    }

    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
}
