/*
**  mod_small_light_graphicsmagick.c -- graphicsmagick support
*/

#include "mod_small_light.h"
#include "mod_small_light_ext_jpeg.h"

small_light_filter_prototype(graphicsmagick);

#ifndef ENABLE_GRAPHICSMAGICK_WAND
small_light_filter_dummy_template(graphicsmagick);
#else

/*
** defines.
*/
#include <wand/magick_wand.h>

typedef struct {
    unsigned char *image;
    apr_size_t image_len;
    MagickWand *wand;
} small_light_module_graphicsmagick_ctx_t;

/*
** functions.
*/

static void small_light_filter_graphicsmagick_output_data_fini(const small_light_module_ctx_t *ctx)
{
    small_light_module_graphicsmagick_ctx_t *lctx = ctx->lctx;
    if (lctx->image != NULL)
    {
        free(lctx->image);
        lctx->image = NULL;
    }
    if (lctx->wand != NULL)
    {
        DestroyMagickWand(lctx->wand);
        lctx->wand = NULL;
        DestroyMagick();
    }
}

apr_status_t small_light_filter_graphicsmagick_init(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_graphicsmagick_init");

    request_rec *r = f->r;
    small_light_module_ctx_t *ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_graphicsmagick_ctx_t *lctx = ctx->lctx;

    // create local context.
    if (ctx->lctx == NULL) {
        ctx->lctx = lctx =
            (small_light_module_graphicsmagick_ctx_t *)apr_pcalloc(
                r->pool, sizeof(small_light_module_graphicsmagick_ctx_t));
    }

    return APR_SUCCESS;
}

apr_status_t small_light_filter_graphicsmagick_receive_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e,
    const char *data,
    apr_size_t len)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_graphicsmagick_receive_data %d", len);
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_graphicsmagick_ctx_t *lctx = ctx->lctx;
    return small_light_receive_data(&lctx->image, &lctx->image_len, f, bb, data, len);
}

// output_data
apr_status_t small_light_filter_graphicsmagick_output_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_graphicsmagick_output_data");

    request_rec *r = f->r;
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_graphicsmagick_ctx_t *lctx = ctx->lctx;
    struct timeval t2, t21, t22, t23, t3;
    MagickPassFail status = MagickPass;

    // check data received.
    if (lctx->image == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "no data received.");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // start image modifing.
    gettimeofday(&t2, NULL);
    small_light_image_size_t sz;
    small_light_calc_image_size(&sz, r, ctx, 10000.0, 10000.0);  // ?

    // init wand (Allocate Wand handle)
    // TODO: get path of client (=apache)
    InitializeMagick("/opt/local/apache2/bin/");  //  InitializeMagick(*argv);
    lctx->wand = NewMagickWand();  // same in Image&Graphics Magick

    // set jpeg hint to wand.
    // TODO: optimize jpeg:size
    if (sz.jpeghint_flg != 0) {
        char *jpeg_size_opt = (char *)apr_psprintf(r->pool, "%dx%d",
                                                   (int)sz.dw, (int)sz.dh);
        /* MagickSetOption(lctx->wand, "jpeg:size", jpeg_size_opt); */

        MagickSetSize(lctx->wand, (long)sz.dw, (long)sz.dh);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MagickSetOption(jpeg:size, %s)", jpeg_size_opt);

        double wand_jpg_w = (double)MagickGetImageWidth(lctx->wand);
        double wand_jpg_h = (double)MagickGetImageHeight(lctx->wand);
        /* char *size_wand_jpg = (char *)apr_psprintf(r->pool, "%fx%f", wand_jpg_w, wand_jpg_h); */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "size_wand_jpg: %fx%f", wand_jpg_w, wand_jpg_h);

    }

    // load image from memory (not file).
    gettimeofday(&t21, NULL);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MagickReadImageBlob");

    status = MagickReadImageBlob(lctx->wand, (void *)lctx->image, lctx->image_len);  // same in Image&Graphics Magick
    if (status == MagickFail) {
        small_light_filter_graphicsmagick_output_data_fini(ctx);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "couldn't read image");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // calc size of original image.
    gettimeofday(&t22, NULL);
    double iw = (double)MagickGetImageWidth(lctx->wand);
    double ih = (double)MagickGetImageHeight(lctx->wand);
    small_light_calc_image_size(&sz, r, ctx, iw, ih);

    // pass through. (check if retval of small_light_calc_image_size() is ok)
    if (sz.pt_flg != 0) {
        small_light_filter_graphicsmagick_output_data_fini(ctx);
        apr_bucket *b = apr_bucket_pool_create(lctx->image, lctx->image_len, r->pool, ctx->bb->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));
        return ap_pass_brigade(f->next, ctx->bb);
    }

    // crop, scale. (resize wand to original-size of loaded image & resize to small_lighted-size)
    status = MagickPass;
    if (sz.scale_flg != 0) {
        char *crop_geo = (char *)apr_psprintf(r->pool, "%f!x%f!+%f+%f",
            sz.sw, sz.sh, sz.sx, sz.sy);
        char *size_geo = (char *)apr_psprintf(r->pool, "%f!x%f!", sz.dw, sz.dh);
        /* char *size_geo = (char *)apr_psprintf(r->pool, "%0.000000!x%0.000000!"); */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickTransformImage(wand, ""%s"", ""%s"")",
            crop_geo, size_geo);
        MagickWand *trans_wand;
        trans_wand = MagickTransformImage(lctx->wand, crop_geo, size_geo);
        if (trans_wand == NULL || trans_wand == lctx->wand) {
            small_light_filter_graphicsmagick_output_data_fini(ctx);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MagickTransformImage failed");
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
        DestroyMagickWand(lctx->wand);
        lctx->wand = trans_wand;
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "no scale");
    }

    // create canvas (background-frame)
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.r: %hx", sz.cc.r);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.g: %hx", sz.cc.g);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.b: %hx", sz.cc.b);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.a: %hx", sz.cc.a);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.r: %f", sz.cc.r / 255.0);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.g: %f", sz.cc.g / 255.0);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.b: %f", sz.cc.b / 255.0);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sz.cc.a: %f", sz.cc.a / 255.0);

    if (sz.cw > 0.0 && sz.ch > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "in create canvas");

        PixelWand *canvas_color;
        canvas_color = NewPixelWand();
        PixelSetRed(canvas_color, sz.cc.r / 255.0);
        PixelSetGreen(canvas_color, sz.cc.g / 255.0);
        PixelSetBlue(canvas_color, sz.cc.b / 255.0);
        PixelSetOpacity(canvas_color, sz.cc.a / 255.0);

        double canvas_border_width;
        double canvas_border_height;

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "cw %lf", sz.cw);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "ch %lf", sz.ch);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "dw %lf", sz.dw);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "dh %lf", sz.dh);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "iw %lf", iw);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "ih %lf", ih);

        canvas_border_width = (sz.cw - sz.dw) / 2;
        canvas_border_height = (sz.ch - sz.dh) / 2;

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "MagickBorderImage(%lf, %lf)",
                      canvas_border_width, canvas_border_height);

        if (canvas_border_width > 0.0 && canvas_border_height > 0.0) {
            status = MagickBorderImage(lctx->wand, canvas_color,
                                       canvas_border_width, canvas_border_height);
        }
        else {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "invalid canvas size");
        }
        DestroyPixelWand(canvas_color);

        if (status == MagickFail) {
            small_light_filter_graphicsmagick_output_data_fini(ctx);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "MagickBorderImage(%lf, %lf) failed",
                          canvas_border_width, canvas_border_height);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
    }

    /* // rotate image */
    /* if (sz.cw > 0.0 && sz.ch > 0.0) { */
    /*     ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "in rotate image"); */

    /*     PixelWand *canvas_color = NewPixelWand(); */
    /*     PixelSetRed(canvas_color, sz.cc.r / 255.0); */
    /*     PixelSetGreen(canvas_color, sz.cc.g / 255.0); */
    /*     PixelSetBlue(canvas_color, sz.cc.b / 255.0); */

    /*     // */
    /*     status = MagickRotateImage(lctx->wand, canvas_color, 30); */
    /*     DestroyPixelWand(canvas_color); */

    /*     if (status == MagickFail) { */
    /*         small_light_filter_graphicsmagick_output_data_fini(ctx); */
    /*         ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, */
    /*                       "MagickRotateImage() failed"); */
    /*         r->status = HTTP_INTERNAL_SERVER_ERROR; */
    /*         return APR_EGENERAL; */
    /*     } */
    /* } */


    // effects.
    // NOTE: because of bug with parsing, enable parameter is sigma only. other parameters are set to defalut.
    char *unsharp = (char *)apr_table_get(ctx->prm, "unsharp");
    if (unsharp) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "unsharp: radius=0.0, sigma=%s, , )", unsharp);
        status = MagickUnsharpMaskImage(lctx->wand, (float)0.0, (float)atof(unsharp), (float)1.0, (float)0.05);
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "unsharp failed");
        }
    }

    char *sharpen = (char *)apr_table_get(ctx->prm, "sharpen");
    if (sharpen) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "sharpen: radius=0.0, sigma=%s)", sharpen);
        status = MagickSharpenImage(lctx->wand, (float)0.0, (float)atof(sharpen));
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "sharpen failed");
        }
    }

    char *blur = (char *)apr_table_get(ctx->prm, "blur");
    if (blur) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "blur: radius=0.0, sigma=%s)", blur);
        status = MagickBlurImage(lctx->wand, (float)0.0, (float)atof(blur));
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "blur failed");
        }
    }

    // border.
    if (sz.bw > 0.0 || sz.bh > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "draw border");

        PixelWand *border_color;
        border_color = NewPixelWand();
        PixelSetRed(border_color, sz.bc.r / 255.0);
        PixelSetGreen(border_color, sz.bc.g / 255.0);
        PixelSetBlue(border_color, sz.bc.b / 255.0);
        PixelSetOpacity(border_color, sz.bc.a / 255.0);

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "bw %lf", sz.bw);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "bh %lf", sz.bh);

        status = MagickBorderImage(lctx->wand, border_color, sz.bw, sz.bh);
        DestroyPixelWand(border_color);

        if (status == MagickFail) {
            small_light_filter_graphicsmagick_output_data_fini(ctx);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "MagickBorderImage(%lf, %lf) failed",
                          sz.bw, sz.bh);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
    }

    gettimeofday(&t23, NULL);


    // set compression quality. default quality is 75 (0 <= q <= 100).
    double q = small_light_parse_double(r, (char *)apr_table_get(ctx->prm, "q"));
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "q %lf", q);
    if (q >= 0.0 && q <= 100.0) {
        char *infile_format = (char *)MagickGetImageFormat(lctx->wand);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "infile_format %s", infile_format);

        /* int infile_q; */
        /* infile_q = MagickGetImageCompression(lctx->wand); */
        /* ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "infile_q %d", infile_q); */

        if (strcmp(infile_format, "JPEG") == 0) {
            status = MagickSetCompressionQuality(lctx->wand, q);
        } else if (strcmp(infile_format, "PNG") == 0) {
            // TODO: enable PNG
            status = MagickSetCompressionQuality(lctx->wand, q);
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                          "unexpected infile_format %s", infile_format);
        }

        if (status == MagickFail) {
            small_light_filter_graphicsmagick_output_data_fini(ctx);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "MagickSetComressionQualty(wand, %f)) failed", q);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
    }

    // set file format like jpeg, png,... default is jpeg
    char *of = (char *)apr_table_get(ctx->prm, "of");
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MagickSetFormat(wand, '%s')", of);
    MagickSetFormat(lctx->wand, of);

    // get small_lighted image as binary.
    unsigned char *canvas_buff;
    const char *sled_image;
    size_t sled_image_size;
    canvas_buff = MagickWriteImageBlob(lctx->wand, &sled_image_size);
    sled_image = (const char *)apr_pmemdup(r->pool, canvas_buff, sled_image_size);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sled_image_size = %d", sled_image_size);

    // free buffer and wand.
    MagickRelinquishMemory(canvas_buff);
    small_light_filter_graphicsmagick_output_data_fini(ctx);

    // insert new bucket to bucket brigade.
    apr_bucket *b = apr_bucket_pool_create(sled_image, sled_image_size, r->pool, ctx->bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(ctx->bb, b);

    // insert eos to bucket brigade.
    APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));

    // set correct Content-Type and Content-Length.
    char *cont_type = apr_psprintf(r->pool, "image/%s", of);
    ap_set_content_type(r, cont_type);
    ap_set_content_length(r, sled_image_size);

    // end.
    gettimeofday(&t3, NULL);

    // http header.
    int info = small_light_parse_int(r, (char *)apr_table_get(ctx->prm, "info"));
    if (info != SMALL_LIGHT_INT_INVALID_VALUE && info != 0) {
        char *info = (char *)apr_psprintf(r->pool,
            "transfer=%ldms, modify image=%ldms (load=%ldms, scale=%ldms, save=%ldms)",
            small_light_timeval_diff(&ctx->t, &t2) / 1000L,
            small_light_timeval_diff(&t2, &t3) / 1000L,
            small_light_timeval_diff(&t21, &t22) / 1000L,
            small_light_timeval_diff(&t22, &t23) / 1000L,
            small_light_timeval_diff(&t23, &t3) / 1000L
        );
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "uri=%s, info=%s)", r->unparsed_uri, info);
        apr_table_setn(r->headers_out, "X-SmallLight-Description", info);
    }

    return ap_pass_brigade(f->next, ctx->bb);
}

#endif
