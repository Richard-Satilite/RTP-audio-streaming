#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ac/common.h"

typedef enum {
    AC_CODEC_L16 = 0,
    AC_CODEC_OPUS = 1
} ac_codec_id_t;

typedef struct {
    ac_codec_id_t codec;
    uint8_t payload_type;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bytes_per_sample;
} ac_codec_info_t;

/*
 * Retorna o número de frames de áudio por canal para uma duração em décimos
 * de milissegundo.
 *
 * Exemplo:
 *   frame_ms_x10 = 25 -> 2,5 ms -> 120 frames em 48 kHz
 *   frame_ms_x10 = 50 -> 5,0 ms -> 240 frames em 48 kHz
 */
size_t ac_codec_frames_for_duration(int frame_ms_x10);

/*
 * Retorna o tamanho em bytes do payload L16 para uma quantidade de frames.
 *
 * Um frame aqui significa uma amostra por canal.
 * Em estéreo s16:
 *   1 frame = 2 canais * 2 bytes = 4 bytes
 */
size_t ac_l16_payload_size(size_t frames);

/*
 * Codifica PCM s16 nativo para RTP L16.
 *
 * Entrada:
 *   pcm          buffer de samples s16 intercalados: L R L R ...
 *   frames       quantidade de frames por canal
 *
 * Saída:
 *   out          payload RTP L16 em big-endian
 *   out_cap      capacidade do buffer de saída
 *
 * Retorno:
 *   número de bytes escritos, ou 0 em erro/capacidade insuficiente.
 */
size_t ac_l16_encode(const int16_t *pcm,
                     size_t frames,
                     uint8_t *out,
                     size_t out_cap);

/*
 * Decodifica payload RTP L16 big-endian para PCM s16 nativo.
 *
 * Entrada:
 *   payload      payload RTP recebido
 *   payload_len  tamanho em bytes
 *
 * Saída:
 *   pcm          buffer de samples s16 intercalados
 *   pcm_cap      capacidade em quantidade de samples int16_t
 *
 * Retorno:
 *   número de frames decodificados, ou 0 em erro/capacidade insuficiente.
 */
size_t ac_l16_decode(const uint8_t *payload,
                     size_t payload_len,
                     int16_t *pcm,
                     size_t pcm_cap);