/** @file
    Decoder for Generic Soil Moisture sensors.

    Copyright (C) 2024 by Community

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/*
    Generic Soil Moisture Sensor
    
    The device uses PWM encoding:
    - 0 is encoded as 504 us pulse and 1308 us gap.
    - 1 is encoded as 504 us pulse and 4000 us gap? (Wait, flex decoder used s=504,l=1308. We should define properly).
    Actually, we will use the user's flex parameters: s=500, l=1300, r=4000.
    
    The data is 65 bits long.
    The payload bytes are completely inverted (!b).
    
    Data layout (inverted bytes):
    II II MM TT SL CC
    
    I: 16-bit Device ID
    M: 8-bit Moisture (0-100%)
    T: 8-bit Temperature (Sign-Magnitude format, bit 7 is negative sign)
    S: 4-bit Status (bits 6-7: Temp Trend, bits 4-5: Battery blocks 0-3)
    L: 4-bit Light level (0-15)
    C: 4-bit Checksum (upper nibble of byte 5)
    P: 4-bit padding (lower nibble of byte 5, always 0)
    E: 1-bit padding (always 0)
*/

#include "decoder.h"

static int generic_soil_moisture_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // The data logic expects inverted bits
    bitbuffer_invert(bitbuffer);

    uint8_t *b;
    int r;
    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        if (bitbuffer->bits_per_row[r] < 65) {
            continue;
        }

        // Preamble is ~0x55 ~0xAA -> inverted 0xAA 0x55
        // Or we might just ignore the preamble if we slice it differently. 
        // Our python script assumed the first 16 bits of the payload were ID, meaning the user's string
        // `{65}55aaa42dff...` had 0x55 0xAA at the start.
        // Wait, if it starts with 0x55 0xAA, we should check it!
        // But inverted 0x55 is 0xAA, inverted 0xAA is 0x55.
        if (b[0] != 0xaa || b[1] != 0x55) {
            continue;
        }

        // Extract payload
        uint8_t payload[6];
        payload[0] = b[2];
        payload[1] = b[3];
        payload[2] = b[4];
        payload[3] = b[5];
        payload[4] = b[6];
        payload[5] = b[7];

        // Checksum
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            sum += (payload[i] >> 4) + (payload[i] & 0x0F);
        }
        int checksum_calc = (sum + 14) % 16;
        int checksum_rx = payload[5] >> 4;

        if (checksum_rx != checksum_calc) {
            decoder_log(decoder, 1, __func__, "checksum error");
            continue; // MIC failed
        }

        int device_id = (payload[0] << 8) | payload[1];
        int moisture = payload[2];
        
        int temp_mag = payload[3] & 0x7F;
        int temp_sign = (payload[3] & 0x80) ? -1 : 1;
        float temperature_c = temp_mag * temp_sign;

        int status_light = payload[4];
        int light = status_light & 0x0F;
        int battery_level = (status_light >> 4) & 0x03;
        int trend = (status_light >> 6) & 0x03;

        int battery_ok = (battery_level > 0) ? 1 : 0; // Simple boolean mapping

        data_t *data = data_make(
            "model",        "", DATA_STRING, "Generic-SoilMoisture",
            "id",           "", DATA_INT,    device_id,
            "battery_ok",   "", DATA_INT,    battery_ok,
            "temperature_C","", DATA_FORMAT, "%.1f", temperature_c,
            "moisture",     "", DATA_INT,    moisture,
            "light",        "", DATA_INT,    light,
            "unknown",      "Trend", DATA_INT, trend,
            "mic",          "", DATA_STRING, "CHECKSUM",
            NULL);

        decoder_output_data(decoder, data);
        return 1;
    }

    return 0;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "battery_ok",
    "temperature_C",
    "moisture",
    "light",
    "unknown",
    "mic",
    NULL,
};

r_device const generic_soil_moisture = {
    .name        = "Generic Soil Moisture Sensor",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 500,
    .long_width  = 1300,
    .reset_limit = 4000,
    .decode_fn   = &generic_soil_moisture_decode,
    .disabled    = 0,
    .fields      = output_fields,
};
