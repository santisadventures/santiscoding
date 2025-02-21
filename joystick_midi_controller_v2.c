#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

// Función para enviar un mensaje MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// Función para mapear el joystick a MIDI, solo para botones de dirección
void map_joystick_to_midi(PmStream *midi_stream, unsigned char *buf, size_t len) {
    // Imprime todos los datos recibidos cada vez que se lee un paquete
    printf("Datos recibidos (len = %zu): ", len);
    for (size_t j = 0; j < len; j++) {
        printf("%02X ", buf[j]);
    }
    printf("\n");

    // Asumiendo que el segundo y tercer byte son relevantes para los pads
    // Pad 1 (por ejemplo, el primer joystick)
    int pad1_up = buf[2] < 0x80;   // Arriba
    int pad1_down = buf[2] > 0x80; // Abajo
    int pad1_left = buf[3] < 0x80; // Izquierda
    int pad1_right = buf[3] > 0x80; // Derecha

    // Pad 2 (por ejemplo, el segundo joystick)
    int pad2_up = buf[4] < 0x80;   // Arriba
    int pad2_down = buf[4] > 0x80; // Abajo
    int pad2_left = buf[5] < 0x80; // Izquierda
    int pad2_right = buf[5] > 0x80; // Derecha

    // Enviar mensajes MIDI para Pad 1
    if (pad1_up) {
        send_midi_message(midi_stream, 0x90, 64, 127); // MIDI para arriba
    }
    if (pad1_down) {
        send_midi_message(midi_stream, 0x90, 65, 127); // MIDI para abajo
    }
    if (pad1_left) {
        send_midi_message(midi_stream, 0x90, 66, 127); // MIDI para izquierda
    }
    if (pad1_right) {
        send_midi_message(midi_stream, 0x90, 67, 127); // MIDI para derecha
    }

    // Enviar mensajes MIDI para Pad 2
    if (pad2_up) {
        send_midi_message(midi_stream, 0x90, 68, 127); // MIDI para arriba (pad 2)
    }
    if (pad2_down) {
        send_midi_message(midi_stream, 0x90, 69, 127); // MIDI para abajo (pad 2)
    }
    if (pad2_left) {
        send_midi_message(midi_stream, 0x90, 70, 127); // MIDI para izquierda (pad 2)
    }
    if (pad2_right) {
        send_midi_message(midi_stream, 0x90, 71, 127); // MIDI para derecha (pad 2)
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

    // Usar Vendor ID y Product ID fijos
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;

    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    handle = hid_open(vendor_id, product_id, NULL);
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
            // Procesar exclusivamente los botones de dirección
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