/**
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
 **                                Stefan Schneider.
 ** (C) 2011 by Kornel Lesinski.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

#include <stdlib.h>

#include "pam.h"
#include "mempool.h"

static hist *pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors, float gamma);
static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance, int* acolorsP);
static void pam_freeacolorhash(acolorhash_table acht);
static acolorhash_table pam_allocacolorhash(void);

int best_color_index(f_pixel px, const colormap *map, float min_opaque_val, float *dist_out)
{
    const colormap_item *const acolormap = map->palette;
    const int numcolors = map->colors;
    int ind=0;
    const int iebug = px.a > min_opaque_val;
    float dist = colordifference(px,acolormap[0].acolor);

    for(int i = 1; i < numcolors; i++) {
        float newdist = colordifference(px,acolormap[i].acolor);

        if (newdist < dist) {

            /* penalty for making holes in IE */
            if (iebug && acolormap[i].acolor.a < 1) {
                if (newdist+1.f/1024.f > dist) continue;
            }

            ind = i;
            dist = newdist;
        }
    }

    if (dist_out) *dist_out = dist;
    return ind;
}

/* libpam3.c - pam (portable alpha map) utility library part 3
 **
 ** Colormap routines.
 **
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997 by Greg Roelofs.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

#define HASH_SIZE 30029

/**
 * Builds color histogram no larger than maxacolors. Ignores (posterizes) ignorebits lower bits in each color.
 * perceptual_weight of each entry is increased by value from importance_map
 */
hist *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance_map)
{
    acolorhash_table acht;
    hist *achv;

    int hist_size=0;
    acht = pam_computeacolorhash(apixels, cols, rows, gamma, maxacolors, ignorebits, importance_map, &hist_size);
    if (!acht) return 0;

    achv = pam_acolorhashtoacolorhist(acht, hist_size, gamma);
    pam_freeacolorhash(acht);
    return achv;
}

static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance_map, int* acolorsP)
{
    acolorhash_table acht;
    struct acolorhist_list_item *achl, **buckets;
    int col, row, hash;
    const unsigned int channel_mask = 255>>ignorebits<<ignorebits;
    const unsigned int posterize_mask = channel_mask << 24 | channel_mask << 16 | channel_mask << 8 | channel_mask;
    acht = pam_allocacolorhash();
    buckets = acht->buckets;
    int colors=0;

    /* Go through the entire image, building a hash table of colors. */
    for (row = 0; row < rows; ++row) {

        float boost=1.0;
        for (col = 0; col < cols; ++col) {
            if (importance_map) {
                boost = 0.5+*importance_map++;
            }

            union rgb_as_long px = {apixels[row][col]};
            px.l &= posterize_mask;
            hash = px.l % HASH_SIZE;

            for (achl = buckets[hash]; achl != NULL; achl = achl->next) {
                if (achl->color.l == px.l)
                    break;
            }

            if (achl != NULL) {
                achl->perceptual_weight += boost;
            } else {
                if (++colors > maxacolors) {
                    pam_freeacolorhash(acht);
                    return NULL;
                }
                achl = mempool_new(&acht->mempool, sizeof(struct acolorhist_list_item));

                achl->color = px;
                achl->perceptual_weight = boost;
                achl->next = buckets[hash];
                buckets[hash] = achl;
            }
        }

    }
    *acolorsP = colors;
    return acht;
}

static acolorhash_table pam_allocacolorhash()
{
    mempool m = NULL;
    acolorhash_table t = mempool_new(&m, sizeof(*t));
    t->buckets = mempool_new(&m, HASH_SIZE * sizeof(t->buckets[0]));
    t->mempool = m;
    return t;
}

static hist *pam_acolorhashtoacolorhist(acolorhash_table acht, int hist_size, float gamma)
{
    hist *hist;
    struct acolorhist_list_item *achl;
    int i, j;

    hist = malloc(sizeof(hist));
    hist->achv = malloc(hist_size * sizeof(hist->achv[0]));
    hist->size = hist_size;

    /* Loop through the hash table. */
    j = 0;
    for (i = 0; i < HASH_SIZE; ++i)
        for (achl = acht->buckets[i]; achl != NULL; achl = achl->next) {
            /* Add the new entry. */
            hist->achv[j].acolor = to_f(gamma, achl->color.rgb);
            hist->achv[j].adjusted_weight = hist->achv[j].perceptual_weight = achl->perceptual_weight;
            ++j;
        }

    /* All done. */
    return hist;
}


static void pam_freeacolorhash(acolorhash_table acht)
{
    mempool_free(acht->mempool);
}

void pam_freeacolorhist(hist *hist)
{
    free(hist->achv);
    free(hist);
}

colormap *pam_colormap(int colors)
{
    colormap *map = malloc(sizeof(colormap));
    map->palette = calloc(colors, sizeof(map->palette[0]));
    map->colors = colors;
    return map;
}

void pam_freecolormap(colormap *c)
{
    free(c->palette); free(c);
}


