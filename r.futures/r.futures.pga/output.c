/*!
   \file outputs.c

   \brief Functions to output rasters

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

#include "output.h"



char *name_for_step(const char *basename, const int step, const int nsteps)
{
    int digits;
    digits = log10(nsteps) + 1;

    return G_generate_basename(basename, step, digits, 0);
}

/*!
    Write current state of developed areas.

    Called at end and dumps tDeveloped for all valid cells (NULL for all others)

    \param undevelopedAsNull Represent undeveloped areas as NULLs instead of -1
    \param developmentAsOne Represent all developed areas as 1 instead of number
        representing the step when are was developed
 */
// TODO: add timestamp to maps
void output_developed_step(SEGMENT *developed_segment, const char *name,
                           int nsteps, bool undeveloped_as_null, bool developed_as_one)
{
    int out_fd;
    int row, col, rows, cols;
    CELL *out_row;
    CELL developed;
    CELL val1, val2;
    struct Colors colors;
    const char *mapset;
    struct History hist;

    rows = Rast_window_rows();
    cols = Rast_window_cols();

    Segment_flush(developed_segment);
    out_fd = Rast_open_new(name, CELL_TYPE);
    out_row = Rast_allocate_c_buf();

    for (row = 0; row < rows; row++) {
        Rast_set_c_null_value(out_row, cols);
        for (col = 0; col < cols; col++) {
            Segment_get(developed_segment, (void *)&developed, row, col);
            if (Rast_is_c_null_value(&developed)) {
                continue;
            }
            /* this handles undeveloped cells */
            if (undeveloped_as_null && developed == -1)
                continue;
            /* this handles developed cells */
            if (developed_as_one)
                developed = 1;
            out_row[col] = developed;
        }
        Rast_put_c_row(out_fd, out_row);
    }
    G_free(out_row);
    Rast_close(out_fd);

    Rast_init_colors(&colors);
    // TODO: the map max is 36 for 36 steps, it is correct?

    if (developed_as_one) {
        val1 = 1;
        val2 = 1;
        Rast_add_c_color_rule(&val1, 255, 100, 50, &val2, 255, 100, 50,
                              &colors);
    }
    else {
        val1 = 0;
        val2 = 0;
        Rast_add_c_color_rule(&val1, 200, 200, 200, &val2, 200, 200, 200,
                              &colors);
        val1 = 1;
        val2 = nsteps;
        Rast_add_c_color_rule(&val1, 255, 100, 50, &val2, 255, 255, 0,
                              &colors);
    }
    if (!undeveloped_as_null) {
        val1 = -1;
        val2 = -1;
        Rast_add_c_color_rule(&val1, 180, 255, 160, &val2, 180, 255, 160,
                              &colors);
    }

    mapset = G_find_file2("cell", name, "");

    if (mapset == NULL)
        G_fatal_error(_("Raster map <%s> not found"), name);

    Rast_write_colors(name, mapset, &colors);
    Rast_free_colors(&colors);

    Rast_short_history(name, "raster", &hist);
    Rast_command_history(&hist);
    // TODO: store also random seed value (need to get it here, global? in Params?)
    Rast_write_history(name, &hist);

    G_message(_("Raster map <%s> created"), name);

}