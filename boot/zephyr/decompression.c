/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <nrf_compress/implementation.h>
#include "compression/decompression.h"
#include "bootutil/crypto/sha.h"
#include "bootutil/bootutil_log.h"

#if !defined(__BOOTSIM__)
#define TARGET_STATIC static
#else
#define TARGET_STATIC
#endif

#if defined(MCUBOOT_SIGN_RSA)
#if MCUBOOT_SIGN_RSA_LEN == 2048
#define EXPECTED_SIG_TLV IMAGE_TLV_RSA2048_PSS
#elif MCUBOOT_SIGN_RSA_LEN == 3072
#define EXPECTED_SIG_TLV IMAGE_TLV_RSA3072_PSS
#endif
#elif defined(MCUBOOT_SIGN_EC256) || \
      defined(MCUBOOT_SIGN_EC384) || \
      defined(MCUBOOT_SIGN_EC)
#define EXPECTED_SIG_TLV IMAGE_TLV_ECDSA_SIG
#elif defined(MCUBOOT_SIGN_ED25519)
#define EXPECTED_SIG_TLV IMAGE_TLV_ED25519
#endif

/* Number of times that consumed data by decompression system can be 0 in a row before aborting */
#define OFFSET_ZERO_CHECK_TIMES 3

BOOT_LOG_MODULE_DECLARE(mcuboot);

static int boot_sha_protected_tlvs(const struct image_header *hdr,
                                   const struct flash_area *fap_src, uint32_t protected_size,
                                   uint8_t *buf, size_t buf_size, bootutil_sha_context *sha_ctx);

bool boot_is_compressed_header_valid(const struct image_header *hdr, const struct flash_area *fap,
                                     struct boot_loader_state *state)
{
    /* Image is compressed in secondary slot, need to check if fits into the primary slot */
    bool opened_flash_area = false;
    int primary_fa_id;
    int rc;
    int size_check;
    int size;
    uint32_t protected_tlvs_size;
    uint32_t decompressed_size;

    if (BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT) == NULL) {
        opened_flash_area = true;
    }

    primary_fa_id = flash_area_id_from_multi_image_slot(BOOT_CURR_IMG(state), BOOT_PRIMARY_SLOT);
    rc = flash_area_open(primary_fa_id, &BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT));
    assert(rc == 0);

    size_check = flash_area_get_size(BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT));

    if (opened_flash_area) {
        (void)flash_area_close(BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT));
    }

    rc = bootutil_get_img_decomp_size(hdr, fap, &decompressed_size);

    if (rc) {
        return false;
    }

    if (!boot_u32_safe_add(&size, decompressed_size, hdr->ih_hdr_size)) {
        return false;
    }

    rc = boot_size_protected_tlvs(hdr, fap, &protected_tlvs_size);

    if (rc) {
        return false;
    }

    if (!boot_u32_safe_add(&size, size, protected_tlvs_size)) {
        return false;
    }

    if (size >= size_check) {
        BOOT_LOG_ERR("Compressed image too large, decompressed image size: 0x%x, slot size: 0x%x",
                     size, size_check);

        return false;
    }

    return true;
}

int bootutil_img_hash_decompress(struct enc_key_data *enc_state, int image_index,
                                 struct image_header *hdr, const struct flash_area *fap,
                                 uint8_t *tmp_buf, uint32_t tmp_buf_sz, uint8_t *hash_result,
                                 uint8_t *seed, int seed_len)
{
    int rc;
    uint32_t read_pos = 0;
    uint32_t write_pos = 0;
    uint32_t protected_tlv_size = 0;
    uint32_t decompressed_image_size;
    struct nrf_compress_implementation *compression = NULL;
    TARGET_STATIC struct image_header modified_hdr;
    bootutil_sha_context sha_ctx;
    uint8_t flash_erased_value;

    bootutil_sha_init(&sha_ctx);

    /* Setup decompression system */
#if CONFIG_NRF_COMPRESS_LZMA_VERSION_LZMA1
    if (!(hdr->ih_flags & IMAGE_F_COMPRESSED_LZMA1)) {
#elif CONFIG_NRF_COMPRESS_LZMA_VERSION_LZMA2
    if (!(hdr->ih_flags & IMAGE_F_COMPRESSED_LZMA2)) {
#endif
        /* Compressed image does not use the correct compression type which is supported by this
         * build
         */
        BOOT_LOG_ERR("Invalid image compression flags: no supported compression found");
        rc = BOOT_EBADIMAGE;

        goto finish_without_clean;
    }

    compression = nrf_compress_implementation_find(NRF_COMPRESS_TYPE_LZMA);

    if (compression == NULL || compression->init == NULL || compression->deinit == NULL ||
        compression->decompress_bytes_needed == NULL || compression->decompress == NULL) {
        /* Compression library missing or missing required function pointer */
        BOOT_LOG_ERR("Decompression library fatal error");
        rc = BOOT_EBADSTATUS;

        goto finish_without_clean;
    }

    rc = compression->init(NULL);

    if (rc) {
        BOOT_LOG_ERR("Decompression library fatal error");
        rc = BOOT_EBADSTATUS;

        goto finish_without_clean;
    }

    /* We need a modified header which has the updated sizes, start with the original header */
    memcpy(&modified_hdr, hdr, sizeof(modified_hdr));

    /* Extract the decompressed image size from the protected TLV, set it and remove the
     * compressed image flags
     */
    rc = bootutil_get_img_decomp_size(hdr, fap, &decompressed_image_size);

    if (rc) {
        BOOT_LOG_ERR("Unable to determine decompressed size of compressed image");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    modified_hdr.ih_flags &= ~COMPRESSIONFLAGS;
    modified_hdr.ih_img_size = decompressed_image_size;

    /* Calculate the protected TLV size, these will not include the decompressed
     * sha/size/signature entries
     */
    rc = boot_size_protected_tlvs(hdr, fap, &protected_tlv_size);

    if (rc) {
        BOOT_LOG_ERR("Unable to determine protected TLV size of compressed image");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    modified_hdr.ih_protect_tlv_size = protected_tlv_size;
    bootutil_sha_update(&sha_ctx, &modified_hdr, sizeof(modified_hdr));
    read_pos = sizeof(modified_hdr);
    flash_erased_value = flash_area_erased_val(fap);
    memset(tmp_buf, flash_erased_value, tmp_buf_sz);

    while (read_pos < modified_hdr.ih_hdr_size) {
        uint32_t copy_size = tmp_buf_sz;

        if ((read_pos + copy_size) > modified_hdr.ih_hdr_size) {
            copy_size = modified_hdr.ih_hdr_size - read_pos;
        }

        bootutil_sha_update(&sha_ctx, tmp_buf, copy_size);
        read_pos += copy_size;
    }

    /* Read in compressed data, decompress and add to hash calculation */
    read_pos = 0;

    while (read_pos < hdr->ih_img_size) {
        uint32_t copy_size = hdr->ih_img_size - read_pos;
        uint32_t tmp_off = 0;
        uint8_t offset_zero_check = 0;

        if (copy_size > tmp_buf_sz) {
            copy_size = tmp_buf_sz;
        }

        rc = flash_area_read(fap, (hdr->ih_hdr_size + read_pos), tmp_buf, copy_size);

        if (rc != 0) {
            BOOT_LOG_ERR("Flash read failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                         (hdr->ih_hdr_size + read_pos), copy_size, fap->fa_id, rc);
            rc = BOOT_EFLASH;

            goto finish;
        }

        /* Decompress data in chunks, writing it back with a larger write offset of the primary
         * slot than read size of the secondary slot
         */
        while (tmp_off < copy_size) {
            uint32_t offset = 0;
            uint8_t *output = NULL;
            uint32_t output_size = 0;
            uint32_t chunk_size;
            bool last_packet = false;

            chunk_size = compression->decompress_bytes_needed(NULL);

            if (chunk_size > (copy_size - tmp_off)) {
                chunk_size = (copy_size - tmp_off);
            }

            if ((read_pos + tmp_off + chunk_size) >= hdr->ih_img_size) {
                last_packet = true;
            }

            rc = compression->decompress(NULL, &tmp_buf[tmp_off], chunk_size, last_packet, &offset,
                                         &output, &output_size);

            if (rc) {
                BOOT_LOG_ERR("Decompression error: %d", rc);
                rc = BOOT_EBADSTATUS;

                goto finish;
            }

            write_pos += output_size;

            if (write_pos > decompressed_image_size) {
                BOOT_LOG_ERR("Decompressed image larger than claimed TLV size, at least: %d",
                             write_pos);
                rc = BOOT_EBADIMAGE;

                goto finish;
            }

            /* Additional dry-run validity checks */
            if (last_packet == true && write_pos == 0) {
                /* Last packet and we still have no output, this is a faulty update */
                BOOT_LOG_ERR("All compressed data consumed without any output, image not valid");
                rc = BOOT_EBADIMAGE;

                goto finish;
            }

            if (offset == 0) {
                /* If the decompression system continually consumes 0 bytes, then there is a
                 * problem with this update image, abort and mark image as bad
                 */
                if (offset_zero_check >= OFFSET_ZERO_CHECK_TIMES) {
                    BOOT_LOG_ERR("Decompression system returning no output data, image not valid");
                    rc = BOOT_EBADIMAGE;

                    goto finish;
                }

                ++offset_zero_check;

                break;
            } else {
                offset_zero_check = 0;
            }

            if (output_size > 0) {
                bootutil_sha_update(&sha_ctx, output, output_size);
            }

            tmp_off += offset;
        }

        read_pos += copy_size;
    }

    /* If there are any protected TLVs present, add them after the main decompressed image */
    if (modified_hdr.ih_protect_tlv_size > 0) {
        rc = boot_sha_protected_tlvs(hdr, fap, modified_hdr.ih_protect_tlv_size, tmp_buf,
                                     tmp_buf_sz, &sha_ctx);
    }

    bootutil_sha_finish(&sha_ctx, hash_result);

finish:
    /* Clean up decompression system */
    (void)compression->deinit(NULL);

finish_without_clean:
    bootutil_sha_drop(&sha_ctx);

    return rc;
}

static int boot_copy_protected_tlvs(const struct image_header *hdr,
                                    const struct flash_area *fap_src,
                                    const struct flash_area *fap_dst, uint32_t off_dst,
                                    uint32_t protected_size, uint8_t *buf, size_t buf_size,
                                    uint16_t *buf_pos, uint32_t *written)
{
    int rc;
    uint32_t off;
    uint32_t write_pos = 0;
    uint16_t len;
    uint16_t type;
    struct image_tlv_iter it;
    struct image_tlv tlv_header;
    struct image_tlv_info tlv_info_header = {
        .it_magic = IMAGE_TLV_PROT_INFO_MAGIC,
        .it_tlv_tot = protected_size,
    };
    uint16_t info_size_left = sizeof(tlv_info_header);

    while (info_size_left > 0) {
        uint16_t copy_size = buf_size - *buf_pos;

        if (info_size_left > 0 && copy_size > 0) {
            uint16_t single_copy_size = copy_size;
            uint8_t *tlv_info_header_address = (uint8_t *)&tlv_info_header;

            if (single_copy_size > info_size_left) {
                single_copy_size = info_size_left;
            }

            memcpy(&buf[*buf_pos], &tlv_info_header_address[sizeof(tlv_info_header) -
                                                            info_size_left], single_copy_size);
            *buf_pos += single_copy_size;
            info_size_left -= single_copy_size;
        }

        if (*buf_pos == buf_size) {
            rc = flash_area_write(fap_dst, (off_dst + write_pos), buf, *buf_pos);

            if (rc != 0) {
                BOOT_LOG_ERR("Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                             (off_dst + write_pos), *buf_pos, fap_dst->fa_id, rc);
                rc = BOOT_EFLASH;

                goto out;
            }

            write_pos += *buf_pos;
            *buf_pos = 0;
        }
    }

    rc = bootutil_tlv_iter_begin(&it, hdr, fap_src, IMAGE_TLV_ANY, true);

    if (rc) {
        goto out;
    }

    while (true) {
        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);

        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            rc = 0;
            break;
        }

        if (type == IMAGE_TLV_DECOMP_SIZE || type == IMAGE_TLV_DECOMP_SHA ||
            type == IMAGE_TLV_DECOMP_SIGNATURE) {
            /* Skip these TLVs as they are not needed */
            continue;
        } else {
            uint16_t header_size_left = sizeof(tlv_header);
            uint16_t data_size_left = len;

            tlv_header.it_type = type;
            tlv_header.it_len = len;

            while (header_size_left > 0 || data_size_left > 0) {
                uint16_t copy_size = buf_size - *buf_pos;
                uint8_t *tlv_header_address = (uint8_t *)&tlv_header;

                if (header_size_left > 0 && copy_size > 0) {
                    uint16_t single_copy_size = copy_size;

                    if (single_copy_size > header_size_left) {
                        single_copy_size = header_size_left;
                    }

                    memcpy(&buf[*buf_pos], &tlv_header_address[sizeof(tlv_header) -
                                                               header_size_left],
                           single_copy_size);
                    *buf_pos += single_copy_size;
                    copy_size -= single_copy_size;
                    header_size_left -= single_copy_size;
                }

                if (data_size_left > 0 && copy_size > 0) {
                    uint16_t single_copy_size = copy_size;

                    if (single_copy_size > data_size_left) {
                        single_copy_size = data_size_left;
                    }

                    rc = LOAD_IMAGE_DATA(hdr, fap_src, (off + (len - data_size_left)),
                                         &buf[*buf_pos], single_copy_size);

                    if (rc) {
                        BOOT_LOG_ERR(
                            "Image data load failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                            (off + (len - data_size_left)), single_copy_size, fap_src->fa_id, rc);

                        goto out;
                    }

                    *buf_pos += single_copy_size;
                    data_size_left -= single_copy_size;
                }

                if (*buf_pos == buf_size) {
                    rc = flash_area_write(fap_dst, (off_dst + write_pos), buf, *buf_pos);

                    if (rc != 0) {
                        BOOT_LOG_ERR(
                           "Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                           (off_dst + write_pos), *buf_pos, fap_dst->fa_id, rc);
                        rc = BOOT_EFLASH;

                        goto out;
                    }

                    write_pos += *buf_pos;
                    *buf_pos = 0;
                }
            }
        }
    }

    *written = write_pos;

out:
    return rc;
}

static int boot_sha_protected_tlvs(const struct image_header *hdr,
                                   const struct flash_area *fap_src, uint32_t protected_size,
                                   uint8_t *buf, size_t buf_size, bootutil_sha_context *sha_ctx)
{
    int rc;
    uint32_t off;
    uint16_t len;
    uint16_t type;
    struct image_tlv_iter it;
    struct image_tlv tlv_header;
    struct image_tlv_info tlv_info_header = {
        .it_magic = IMAGE_TLV_PROT_INFO_MAGIC,
        .it_tlv_tot = protected_size,
    };

    bootutil_sha_update(sha_ctx, &tlv_info_header, sizeof(tlv_info_header));

    rc = bootutil_tlv_iter_begin(&it, hdr, fap_src, IMAGE_TLV_ANY, true);
    if (rc) {
        goto out;
    }

    while (true) {
        uint32_t read_off = 0;

        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);

        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            rc = 0;
            break;
        }

        if (type == IMAGE_TLV_DECOMP_SIZE || type == IMAGE_TLV_DECOMP_SHA ||
            type == IMAGE_TLV_DECOMP_SIGNATURE) {
            /* Skip these TLVs as they are not needed */
            continue;
        }

        tlv_header.it_type = type;
        tlv_header.it_len = len;

        bootutil_sha_update(sha_ctx, &tlv_header, sizeof(tlv_header));

        while (read_off < len) {
            uint32_t copy_size = buf_size;

            if (copy_size > (len - read_off)) {
                copy_size = len - read_off;
            }

            rc = LOAD_IMAGE_DATA(hdr, fap_src, (off + read_off), buf, copy_size);

            if (rc) {
                BOOT_LOG_ERR(
                    "Image data load failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                    (off + read_off), copy_size, fap_src->fa_id, rc);

                goto out;
            }

            bootutil_sha_update(sha_ctx, buf, copy_size);
            read_off += copy_size;
        }
    }

out:
    return rc;
}

int boot_size_protected_tlvs(const struct image_header *hdr, const struct flash_area *fap,
                             uint32_t *sz)
{
    int rc = 0;
    uint32_t tlv_size;
    uint32_t off;
    uint16_t len;
    uint16_t type;
    struct image_tlv_iter it;

    *sz = 0;
    tlv_size = hdr->ih_protect_tlv_size;

    rc = bootutil_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_ANY, true);

    if (rc) {
        goto out;
    }

    while (true) {
        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);

        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            rc = 0;
            break;
        }

        if (type == IMAGE_TLV_DECOMP_SIZE || type == IMAGE_TLV_DECOMP_SHA ||
            type == IMAGE_TLV_DECOMP_SIGNATURE) {
            /* Exclude these TLVs as they will be copied to the unprotected area */
            tlv_size -= len + sizeof(struct image_tlv);
        }
    }

    if (!rc) {
        if (tlv_size == sizeof(struct image_tlv_info)) {
            /* If there are no entries then omit protected TLV section entirely */
            tlv_size = 0;
        }

        *sz = tlv_size;
    }

out:
    return rc;
}

int boot_size_unprotected_tlvs(const struct image_header *hdr, const struct flash_area *fap,
                               uint32_t *sz)
{
    int rc = 0;
    uint32_t tlv_size;
    uint32_t off;
    uint16_t len;
    uint16_t type;
    struct image_tlv_iter it;

    *sz = 0;
    tlv_size = sizeof(struct image_tlv_info);

    rc = bootutil_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_ANY, false);

    if (rc) {
        goto out;
    }

    while (true) {
        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);

        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            rc = 0;
            break;
        } else if (bootutil_tlv_iter_is_prot(&it, off)) {
            continue;
        }

        tlv_size += len + sizeof(struct image_tlv);
    }

    if (!rc) {
        if (tlv_size == sizeof(struct image_tlv_info)) {
            /* If there are no entries in the unprotected TLV section then there is something wrong
             * with this image
             */
            BOOT_LOG_ERR("No unprotected TLVs in post-decompressed image output, image is invalid");
            rc = BOOT_EBADIMAGE;

            goto out;
        }

        *sz = tlv_size;
    }

out:
    return rc;
}

static int boot_copy_unprotected_tlvs(const struct image_header *hdr,
                                      const struct flash_area *fap_src,
                                      const struct flash_area *fap_dst, uint32_t off_dst,
                                      uint32_t unprotected_size, uint8_t *buf, size_t buf_size,
                                      uint16_t *buf_pos, uint32_t *written)
{
    int rc;
    uint32_t write_pos = 0;
    uint32_t off;
    uint16_t len;
    uint16_t type;
    struct image_tlv_iter it;
    struct image_tlv_iter it_protected;
    struct image_tlv tlv_header;
    struct image_tlv_info tlv_info_header = {
        .it_magic = IMAGE_TLV_INFO_MAGIC,
        .it_tlv_tot = unprotected_size,
    };
    uint16_t info_size_left = sizeof(tlv_info_header);

    while (info_size_left > 0) {
        uint16_t copy_size = buf_size - *buf_pos;

        if (info_size_left > 0 && copy_size > 0) {
            uint16_t single_copy_size = copy_size;
            uint8_t *tlv_info_header_address = (uint8_t *)&tlv_info_header;

            if (single_copy_size > info_size_left) {
                single_copy_size = info_size_left;
            }

            memcpy(&buf[*buf_pos], &tlv_info_header_address[sizeof(tlv_info_header) -
                                                            info_size_left], single_copy_size);
            *buf_pos += single_copy_size;
            info_size_left -= single_copy_size;
        }

        if (*buf_pos == buf_size) {
            rc = flash_area_write(fap_dst, (off_dst + write_pos), buf, *buf_pos);

            if (rc != 0) {
                BOOT_LOG_ERR("Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                             (off_dst + write_pos), *buf_pos, fap_dst->fa_id, rc);
                rc = BOOT_EFLASH;

                goto out;
            }

            write_pos += *buf_pos;
            *buf_pos = 0;
        }
    }

    rc = bootutil_tlv_iter_begin(&it, hdr, fap_src, IMAGE_TLV_ANY, false);
    if (rc) {
        goto out;
    }

    while (true) {
        uint16_t header_size_left = sizeof(tlv_header);
        uint16_t data_size_left;

        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);
        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            rc = 0;
            break;
        } else if (bootutil_tlv_iter_is_prot(&it, off)) {
            /* Skip protected TLVs */
            continue;
        }

        /* Change the values of these fields from having the data in the compressed image
         * unprotected TLV (which is valid only for the compressed image data) to having the
         * fields in the protected TLV section (which is valid for the decompressed image data).
         * The compressed data is no longer needed
         */
        if (type == EXPECTED_HASH_TLV || type == EXPECTED_SIG_TLV) {
            rc = bootutil_tlv_iter_begin(&it_protected, hdr, fap_src, (type == EXPECTED_HASH_TLV ?
                                                                       IMAGE_TLV_DECOMP_SHA :
                                                                       IMAGE_TLV_DECOMP_SIGNATURE),
                                         true);

            if (rc) {
                goto out;
            }

            while (true) {
                rc = bootutil_tlv_iter_next(&it_protected, &off, &len, &type);
                if (rc < 0) {
                    goto out;
                } else if (rc > 0) {
                    rc = 0;
                    break;
                }
            }

            if (type == IMAGE_TLV_DECOMP_SHA) {
                type = EXPECTED_HASH_TLV;
            } else {
                type = EXPECTED_SIG_TLV;
            }
        }

        data_size_left = len;
        tlv_header.it_type = type;
        tlv_header.it_len = len;

        while (header_size_left > 0 || data_size_left > 0) {
            uint16_t copy_size = buf_size - *buf_pos;

            if (header_size_left > 0 && copy_size > 0) {
                uint16_t single_copy_size = copy_size;
                uint8_t *tlv_header_address = (uint8_t *)&tlv_header;

                if (single_copy_size > header_size_left) {
                    single_copy_size = header_size_left;
                }

                memcpy(&buf[*buf_pos], &tlv_header_address[sizeof(tlv_header) - header_size_left],
                       single_copy_size);
                *buf_pos += single_copy_size;
                copy_size -= single_copy_size;
                header_size_left -= single_copy_size;
            }

            if (data_size_left > 0 && copy_size > 0) {
                uint16_t single_copy_size = copy_size;

                if (single_copy_size > data_size_left) {
                    single_copy_size = data_size_left;
                }

                rc = LOAD_IMAGE_DATA(hdr, fap_src, (off + len - data_size_left),
                                     &buf[*buf_pos], single_copy_size);

                if (rc) {
                    BOOT_LOG_ERR(
                        "Image data load failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                        (off + (len - data_size_left)), single_copy_size, fap_src->fa_id, rc);

                    goto out;
                }

                *buf_pos += single_copy_size;
                data_size_left -= single_copy_size;
            }

            if (*buf_pos == buf_size) {
                rc = flash_area_write(fap_dst, (off_dst + write_pos), buf, *buf_pos);

                if (rc != 0) {
                    BOOT_LOG_ERR(
                        "Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                        (off_dst + write_pos), *buf_pos, fap_dst->fa_id, rc);
                    rc = BOOT_EFLASH;

                    goto out;
                }

                write_pos += *buf_pos;
                *buf_pos = 0;
            }
        }
    }

    *written = write_pos;

out:
    return rc;
}

int boot_copy_region_decompress(struct boot_loader_state *state, const struct flash_area *fap_src,
                                const struct flash_area *fap_dst, uint32_t off_src,
                                uint32_t off_dst, uint32_t sz, uint8_t *buf, size_t buf_size)
{
    int rc;
    uint32_t pos = 0;
    uint16_t decomp_buf_size = 0;
    uint16_t write_alignment;
    uint32_t write_pos = 0;
    uint32_t protected_tlv_size = 0;
    uint32_t unprotected_tlv_size = 0;
    uint32_t tlv_write_size = 0;
    uint32_t decompressed_image_size;
    struct nrf_compress_implementation *compression = NULL;
    struct image_header *hdr;
    TARGET_STATIC uint8_t decomp_buf[CONFIG_BOOT_DECOMPRESSION_BUFFER_SIZE] __attribute__((aligned(4)));
    TARGET_STATIC struct image_header modified_hdr;

    hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);

    /* Setup decompression system */
#if CONFIG_NRF_COMPRESS_LZMA_VERSION_LZMA1
    if (!(hdr->ih_flags & IMAGE_F_COMPRESSED_LZMA1)) {
#elif CONFIG_NRF_COMPRESS_LZMA_VERSION_LZMA2
    if (!(hdr->ih_flags & IMAGE_F_COMPRESSED_LZMA2)) {
#endif
        /* Compressed image does not use the correct compression type which is supported by this
         * build
         */
        BOOT_LOG_ERR("Invalid image compression flags: no supported compression found");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    compression = nrf_compress_implementation_find(NRF_COMPRESS_TYPE_LZMA);

    if (compression == NULL || compression->init == NULL || compression->deinit == NULL ||
        compression->decompress_bytes_needed == NULL || compression->decompress == NULL) {
        /* Compression library missing or missing required function pointer */
        BOOT_LOG_ERR("Decompression library fatal error");
        rc = BOOT_EBADSTATUS;

        goto finish;
    }

    rc = compression->init(NULL);

    if (rc) {
        BOOT_LOG_ERR("Decompression library fatal error");
        rc = BOOT_EBADSTATUS;

        goto finish;
    }

    write_alignment = flash_area_align(fap_dst);

    memcpy(&modified_hdr, hdr, sizeof(modified_hdr));

    rc = bootutil_get_img_decomp_size(hdr, fap_src, &decompressed_image_size);

    if (rc) {
        BOOT_LOG_ERR("Unable to determine decompressed size of compressed image");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    modified_hdr.ih_flags &= ~COMPRESSIONFLAGS;
    modified_hdr.ih_img_size = decompressed_image_size;

    /* Calculate protected TLV size for target image once items are removed */
    rc = boot_size_protected_tlvs(hdr, fap_src, &protected_tlv_size);

    if (rc) {
        BOOT_LOG_ERR("Unable to determine protected TLV size of compressed image");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    modified_hdr.ih_protect_tlv_size = protected_tlv_size;

    rc = boot_size_unprotected_tlvs(hdr, fap_src, &unprotected_tlv_size);

    if (rc) {
        BOOT_LOG_ERR("Unable to determine unprotected TLV size of compressed image");
        rc = BOOT_EBADIMAGE;

        goto finish;
    }

    /* Write out the image header first, this should be a multiple of the write size */
    rc = flash_area_write(fap_dst, off_dst, &modified_hdr, sizeof(modified_hdr));

    if (rc != 0) {
        BOOT_LOG_ERR("Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                     off_dst, sizeof(modified_hdr), fap_dst->fa_id, rc);
        rc = BOOT_EFLASH;

        goto finish;
    }

    /* Read in, decompress and write out data */
    while (pos < hdr->ih_img_size) {
        uint32_t copy_size = hdr->ih_img_size - pos;
        uint32_t tmp_off = 0;

        if (copy_size > buf_size) {
            copy_size = buf_size;
        }

        rc = flash_area_read(fap_src, off_src + hdr->ih_hdr_size + pos, buf, copy_size);

        if (rc != 0) {
            BOOT_LOG_ERR("Flash read failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                         (off_src + hdr->ih_hdr_size + pos), copy_size, fap_src->fa_id, rc);
            rc = BOOT_EFLASH;

            goto finish;
        }

        /* Decompress data in chunks, writing it back with a larger write offset of the primary
         * slot than read size of the secondary slot
         */
        while (tmp_off < copy_size) {
            uint32_t offset = 0;
            uint32_t output_size = 0;
            uint32_t chunk_size;
            uint32_t compression_buffer_pos = 0;
            uint8_t *output = NULL;
            bool last_packet = false;

            chunk_size = compression->decompress_bytes_needed(NULL);

            if (chunk_size > (copy_size - tmp_off)) {
                chunk_size = (copy_size - tmp_off);
            }

            if ((pos + tmp_off + chunk_size) >= hdr->ih_img_size) {
                last_packet = true;
            }

            rc = compression->decompress(NULL, &buf[tmp_off], chunk_size, last_packet, &offset,
                                         &output, &output_size);

            if (rc) {
                BOOT_LOG_ERR("Decompression error: %d", rc);
                rc = BOOT_EBADSTATUS;

                goto finish;
            }

            /* Copy data to secondary buffer for writing out */
            while (output_size > 0) {
                uint32_t data_size = (sizeof(decomp_buf) - decomp_buf_size);

                if (data_size > output_size) {
                    data_size = output_size;
                }

                memcpy(&decomp_buf[decomp_buf_size], &output[compression_buffer_pos], data_size);
                compression_buffer_pos += data_size;

                decomp_buf_size += data_size;
                output_size -= data_size;

                /* Write data out from secondary buffer when it is full */
                if (decomp_buf_size == sizeof(decomp_buf)) {
                    rc = flash_area_write(fap_dst, (off_dst + hdr->ih_hdr_size + write_pos),
                                          decomp_buf, sizeof(decomp_buf));

                    if (rc != 0) {
                        BOOT_LOG_ERR(
                            "Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                            (off_dst + hdr->ih_hdr_size + write_pos), sizeof(decomp_buf),
                            fap_dst->fa_id, rc);
                        rc = BOOT_EFLASH;

                        goto finish;
                    }

                    write_pos += sizeof(decomp_buf);
                    decomp_buf_size = 0;
                }
            }

            tmp_off += offset;
        }

        pos += copy_size;
    }

    /* Clean up decompression system */
    (void)compression->deinit(NULL);

    if (protected_tlv_size > 0) {
        rc = boot_copy_protected_tlvs(hdr, fap_src, fap_dst, (off_dst + hdr->ih_hdr_size +
                                                              write_pos), protected_tlv_size,
                                      decomp_buf, sizeof(decomp_buf_size), &decomp_buf_size,
                                      &tlv_write_size);

        if (rc) {
            BOOT_LOG_ERR("Protected TLV copy failure: %d", rc);

            goto finish;
        }

        write_pos += tlv_write_size;
    }

    tlv_write_size = 0;
    rc = boot_copy_unprotected_tlvs(hdr, fap_src, fap_dst, (off_dst + hdr->ih_hdr_size +
                                                            write_pos), unprotected_tlv_size,
                                    decomp_buf, sizeof(decomp_buf_size), &decomp_buf_size,
                                    &tlv_write_size);

    if (rc) {
        BOOT_LOG_ERR("Protected TLV copy failure: %d", rc);

        goto finish;
    }

    write_pos += tlv_write_size;

    /* Check if we have unwritten data buffered up and, if so, write it out */
    if (decomp_buf_size > 0) {
        uint32_t write_padding_size = decomp_buf_size % write_alignment;

        /* Check if additional write padding should be applied to meet the minimum write size */
        if (write_padding_size) {
            uint8_t flash_erased_value;

            flash_erased_value = flash_area_erased_val(fap_dst);
            memset(&decomp_buf[decomp_buf_size], flash_erased_value, write_padding_size);
            decomp_buf_size += write_padding_size;
        }

        rc = flash_area_write(fap_dst, (off_dst + hdr->ih_hdr_size + write_pos), decomp_buf,
                              decomp_buf_size);

        if (rc != 0) {
            BOOT_LOG_ERR("Flash write failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                         (off_dst + hdr->ih_hdr_size + write_pos), sizeof(decomp_buf_size),
                         fap_dst->fa_id, rc);
            rc = BOOT_EFLASH;

            goto finish;
        }

        write_pos += decomp_buf_size;
        decomp_buf_size = 0;
    }

finish:
    memset(decomp_buf, 0, sizeof(decomp_buf));

    return rc;
}

int bootutil_get_img_decomp_size(const struct image_header *hdr, const struct flash_area *fap,
                                 uint32_t *img_decomp_size)
{
    struct image_tlv_iter it;
    uint32_t off;
    uint16_t len;
    int32_t rc;

    if (hdr == NULL || fap == NULL || img_decomp_size == NULL) {
        return BOOT_EBADARGS;
    } else if (hdr->ih_protect_tlv_size == 0) {
        return BOOT_EBADIMAGE;
    }

    rc = bootutil_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_DECOMP_SIZE, true);

    if (rc) {
        return rc;
    }

    rc = bootutil_tlv_iter_next(&it, &off, &len, NULL);

    if (rc != 0) {
        return -1;
    }

    if (len != sizeof(*img_decomp_size)) {
        BOOT_LOG_ERR("Invalid decompressed image size TLV: %d", len);

        return BOOT_EBADIMAGE;
    }

    rc = LOAD_IMAGE_DATA(hdr, fap, off, img_decomp_size, len);

    if (rc) {
        BOOT_LOG_ERR("Image data load failed at offset: 0x%x, size: 0x%x, area: %d, rc: %d",
                     off, len, fap->fa_id, rc);

        return BOOT_EFLASH;
    }

    return 0;
}
