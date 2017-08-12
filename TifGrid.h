#ifndef TIF_GRID_H
#define TIF_GRID_H

#include "Grid.h"

FloatGrid *ReadFloatTifGrid(const char *file, double top, double bottom, double left, double right, bool *outside = NULL);
FloatGrid *ReadFloatTifGrid(const char *file, FloatGrid *incGrid, double top, double bottom, double left, double right, bool *outside = NULL);
void WriteFloatTifGrid(const char *file, FloatGrid *grid, const char *artist = NULL, const char *datetime = NULL, const char *copyright = NULL);
LongGrid *ReadLongTifGrid(const char *file);

#endif
