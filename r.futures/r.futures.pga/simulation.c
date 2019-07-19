/*!
   \file simulation.c

   \brief Higher-level functions simulating urban growth

   (C) 2016-2019 by Anna Petrasova, Vaclav Petras and the GRASS Development Team

   This program is free software under the GNU General Public License
   (>=v2).  Read the file COPYING that comes with GRASS for details.

   \author Anna Petrasova
   \author Vaclav Petras
 */

#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include <grass/segment.h>

#include "inputs.h"
#include "devpressure.h"
#include "simulation.h"


int find_probable_seed(struct Undeveloped *undev_cells, int region)
{
    int first, last, middle;
    double p;

    p = G_drand48();
    // bisect
    first = 0;
    last = undev_cells->num[region] - 1;
    middle = (first + last) / 2;
    if (p <= undev_cells->cells[region][first].cumulative_probability)
        return 0;
    if (p >= undev_cells->cells[region][last].cumulative_probability)
        return last;
    while (first <= last) {
        if (undev_cells->cells[region][middle].cumulative_probability < p)
            first = middle + 1;
        else if (undev_cells->cells[region][middle - 1].cumulative_probability < p &&
                 undev_cells->cells[region][middle].cumulative_probability >= p) {
            return middle;
        }
        else
            last = middle - 1;
        middle = (first + last)/2;
    }
    // TODO: returning at least something but should be something more meaningful
    return 0;
}


int get_seed(struct Undeveloped *undev_cells, int region_idx, enum seed_search method,
              int *row, int *col)
{
    int i, id;
    if (method == RANDOM)
        i = (int)(G_drand48() * undev_cells->max[region_idx]);
    else
        i = find_probable_seed(undev_cells, region_idx);
    id = undev_cells->cells[region_idx][i].id;
    get_xy_from_idx(id, Rast_window_cols(), row, col);
    return i;
}



double get_develop_probability_xy(struct Segments *segments,
                                  FCELL *values,
                                  struct Potential *potential_info,
                                  int region_index, int row, int col)
{
    float probability;
    int i;
    int transformed_idx = 0;
    FCELL devpressure_val;
    FCELL weight;

    Segment_get(&segments->devpressure, (void *)&devpressure_val, row, col);
    Segment_get(&segments->predictors, values, row, col);
    
    probability = potential_info->intercept[region_index];
    probability += potential_info->devpressure[region_index] * devpressure_val;
    for (i = 0; i < potential_info->max_predictors; i++) {
        probability += potential_info->predictors[i][region_index] * values[i];
    }
    probability = 1.0 / (1.0 + exp(-probability));
    if (potential_info->incentive_transform) {
        transformed_idx = (int) (probability * (potential_info->incentive_transform_size - 1));
        if (transformed_idx >= potential_info->incentive_transform_size || transformed_idx < 0)
            G_fatal_error("lookup position (%d) out of range [0, %d]",
                          transformed_idx, potential_info->incentive_transform_size - 1);
        probability = potential_info->incentive_transform[transformed_idx];
    }
    
    /* weights if applicable */
    if (segments->use_weight) {
        Segment_get(&segments->weight, (void *)&weight, row, col);
        if (weight < 0)
            probability *= fabs(weight);
        else if (weight > 0)
            probability = probability + weight - probability * weight;
    }
    return probability;
}


void recompute_probabilities(struct Undeveloped *undeveloped_cells,
                             struct Segments *segments,
                             struct Potential *potential_info)
{
    int row, col, cols, rows;
    int id, i, idx, new_size;
    int region_idx;
    CELL developed;
    CELL region;
    FCELL *values;
    float probability;
    float sum;
    
    cols = Rast_window_cols();
    rows = Rast_window_rows();
    values = G_malloc(potential_info->max_predictors * sizeof(FCELL *));
    
    for (region_idx = 0; region_idx < undeveloped_cells->max_subregions; region_idx++) {
        undeveloped_cells->num[region_idx] = 0;
    }
    for (row = 0; row < rows; row++) {
        for (col = 0; col < cols; col++) {
            Segment_get(&segments->developed, (void *)&developed, row, col);
            if (Rast_is_null_value(&developed, CELL_TYPE))
                continue;
            if (developed != -1)
                continue;
            Segment_get(&segments->subregions, (void *)&region, row, col);
            if (Rast_is_null_value(&region, CELL_TYPE))
                continue;
            
            /* realloc if needed */
            if (undeveloped_cells->num[region] >= undeveloped_cells->max[region]) {
                new_size = 2 * undeveloped_cells->max[region];
                undeveloped_cells->cells[region] = 
                        (struct UndevelopedCell *) G_realloc(undeveloped_cells->cells[region],
                                                             new_size * sizeof(struct UndevelopedCell));
                undeveloped_cells->max[region] = new_size;
            }
            id = get_idx_from_xy(row, col, cols);
            idx = undeveloped_cells->num[region];
            undeveloped_cells->cells[region][idx].id = id;
            undeveloped_cells->cells[region][idx].tried = 0;
            /* get probability and update undevs and segment*/
            probability = get_develop_probability_xy(segments, values,
                                                     potential_info, region, row, col);
            Segment_put(&segments->probability, (void *)&probability, row, col);
            undeveloped_cells->cells[region][idx].probability = probability;
            
            undeveloped_cells->num[region]++;
            
        }
    }

    i = 0;
    for (region_idx = 0; region_idx < undeveloped_cells->max_subregions; region_idx++) {
        probability = undeveloped_cells->cells[region_idx][0].probability;
        undeveloped_cells->cells[region_idx][0].cumulative_probability = probability;
        for (i = 1; i < undeveloped_cells->num[region_idx]; i++) {
            probability = undeveloped_cells->cells[region_idx][i].probability;
            undeveloped_cells->cells[region_idx][i].cumulative_probability = 
                    undeveloped_cells->cells[region_idx][i - 1].cumulative_probability + probability;
        }
        sum = undeveloped_cells->cells[region_idx][i - 1].cumulative_probability;
        for (i = 0; i < undeveloped_cells->num[region_idx]; i++) {
            undeveloped_cells->cells[region_idx][i].cumulative_probability /= sum;
        }
    }
}

void compute_step(struct Undeveloped *undev_cells, struct Demand *demand,
                  enum seed_search search_alg,
                  struct Segments *segments,
                  struct PatchSizes *patch_sizes, struct PatchInfo *patch_info,
                  struct DevPressure *devpressure_info, int *patch_overflow,
                  int step, int region)
{
    int i, idx;
    int n_to_convert;
    int n_done;
    int found;
    int seed_row, seed_col;
    int row, col;
    int patch_size;
    int *added_ids;
    bool force_convert_all;
    int extra;
    bool allow_already_tried_ones;
    int unsuccessful_tries;
    FCELL prob;
    CELL developed;


    added_ids = (int *) G_malloc(sizeof(int) * patch_sizes->max_patch_size);
    n_to_convert = demand->table[region][step];
    n_done = 0;
    force_convert_all = false;
    allow_already_tried_ones = false;
    unsuccessful_tries = 0;
    extra = patch_overflow[region];

    if (extra > 0) {
        if (n_to_convert - extra > 0) {
            n_to_convert -= extra;
            extra = 0;
        }
        else {
            extra -= n_to_convert;
            n_to_convert = 0;
        }
    }

    if (n_to_convert > undev_cells->num[region]) {
        G_warning("Not enough undeveloped cells in region %d (requested: %d,"
                  " available: %d). Converting all available.",
                   region, n_to_convert, undev_cells->num[region]);
        n_to_convert =  undev_cells->num[region];
        force_convert_all = true;
    }
    
    while (n_done < n_to_convert) {
        /* if we can't find a seed, turn off the restriction to use only untried ones */
        if (!allow_already_tried_ones && unsuccessful_tries > MAX_SEED_ITER * n_to_convert)
            allow_already_tried_ones = true;

        /* get seed's row, col and index in undev cells array */
        idx = get_seed(undev_cells, region, search_alg, &seed_row, &seed_col);
        /* skip if seed was already tried unless we switched of this check because we can't get any seed */
        if (!allow_already_tried_ones && undev_cells->cells[region][idx].tried) {
            unsuccessful_tries++;
            continue;
        }
        /* mark as tried */
        undev_cells->cells[region][idx].tried = 1;
        /* see if seed was already developed during this time step */
        Segment_get(&segments->developed, (void *)&developed, seed_row, seed_col);
        if (developed != -1) {
            unsuccessful_tries++;
            continue;
        }
        /* get probability */
        Segment_get(&segments->probability, (void *)&prob, seed_row, seed_col);
        /* challenge probability unless we need to convert all */
        if(force_convert_all || G_drand48() < prob) {
            /* ger random patch size */
            patch_size = get_patch_size(patch_sizes);
            /* grow patch and return the actual grown size which could be smaller */
            found = grow_patch(seed_row, seed_col, patch_size, step,
                               patch_info, segments, added_ids);
            /* update devpressure for every newly developed cell */
            for (i = 0; i < found; i++) {
                get_xy_from_idx(added_ids[i], Rast_window_cols(), &row, &col);
                update_development_pressure_precomputed(row, col, segments, devpressure_info);
            }
            n_done += found;
        }
    }
    extra += (n_done - n_to_convert);
    patch_overflow[region] = extra;
    G_debug(2, "There are %d extra cells for next timestep", extra);
    G_free(added_ids);
}
