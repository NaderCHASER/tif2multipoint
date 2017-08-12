#include <limits>
#include <cstdio>
#include <stdlib.h>
#include "xtiffio.h"
#include "geotiffio.h"
#include "Messages.h"
#include "Defines.h"
#include "TifGrid.h"

#define TIFFTAG_GDAL_METADATA 42112
#define TIFFTAG_GDAL_NODATA 42113

static const TIFFFieldInfo xtiffFieldInfo[] = {
  { TIFFTAG_GDAL_METADATA, -1,-1, TIFF_ASCII, FIELD_CUSTOM, true, false, (char*) "GDALMetadata" },
  { TIFFTAG_GDAL_NODATA, -1,-1, TIFF_ASCII, FIELD_CUSTOM, true, false, (char*) "GDALNoDataValue" }
};

static TIFFExtendProc TIFFParentExtender = NULL;
static void TIFFExtenderInit();
static void TIFFDefaultDirectory(TIFF *tif);


static void TIFFExtenderInit() {
  static int first_time=1;
  
  if (!first_time) {
    return; /* Been there. Done that. */
  }
  first_time = 0;
  
  /* Grab the inherited method and install */
  TIFFParentExtender = TIFFSetTagExtender(TIFFDefaultDirectory);
  
  TIFFSetErrorHandler(NULL);
}

static void TIFFDefaultDirectory(TIFF *tif) {
  /* Install the extended Tag field info */
  TIFFMergeFieldInfo(tif, xtiffFieldInfo, sizeof(xtiffFieldInfo) / sizeof(xtiffFieldInfo[0]));
  
  /* Since an XTIFF client module may have overridden
   *      * the default directory method, we call it now to
   *           * allow it to set up the rest of its own methods.
   *                */
  
  if (TIFFParentExtender) {
    (*TIFFParentExtender)(tif);
  }
}

FloatGrid *ReadFloatTifGrid(const char *file, double top, double bottom, double left, double right, bool *outside) {
  return ReadFloatTifGrid(file, NULL, top, bottom, left, right, outside);
}

FloatGrid *ReadFloatTifGrid(const char *file, FloatGrid *incGrid, double top, double bottom, double left, double right, bool *outside) {
  
  TIFFExtenderInit();
  
  FloatGrid *grid = incGrid;
  TIFF *tif = NULL;
  GTIF *gtif = NULL;
 
  if (outside) {
    *outside = false;
  }
 
  tif = XTIFFOpen(file, "r");
  if (!tif) {
    return NULL;
  }
  
  gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return NULL;
  }
  
  unsigned short sampleFormat, samplesPerPixel, bitsPerSample;
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  
  if (sampleFormat != SAMPLEFORMAT_IEEEFP || bitsPerSample != 32 || samplesPerPixel != 1) {
    WARNING_LOGF("%s is not a supported Float32 GeoTiff", file);
    GTIFFree(gtif);
    XTIFFClose(tif);
    return NULL;
  }
  
  int width, height;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  
  short tiepointsize, pixscalesize;
  double* tiepoints;//[6];
  double* pixscale;//[3];
  TIFFGetField(tif, TIFFTAG_GEOTIEPOINTS,  &tiepointsize,
               &tiepoints);
  TIFFGetField(tif, TIFFTAG_GEOPIXELSCALE, &pixscalesize,
               &pixscale);

  BoundingBox gridBB;
  gridBB.top = tiepoints[4];
  gridBB.left = tiepoints[3];
  gridBB.bottom = tiepoints[4]-(pixscale[1] * float(height));
  gridBB.right = tiepoints[3]+(pixscale[0] * float(width));
  BoundingBox tileBB;
  tileBB.top = top;
  tileBB.left = left;
  tileBB.bottom = bottom;
  tileBB.right = right;

  if (!tileBB.Intersects(&gridBB)) {
	/*WARNING_LOGF("Tile bounding box does not intersect %s", file);
	WARNING_LOGF("Tile bounding box %f %f, %f %f", tileBB.top, tileBB.bottom, tileBB.left, tileBB.right);
	WARNING_LOGF("Grid bounding box %f %f, %f %f", gridBB.top, gridBB.bottom, gridBB.left, gridBB.right);*/
        if (outside) {
          *outside = true;
        }
 	GTIFFree(gtif);
	XTIFFClose(tif);
	return NULL;
   }
 
  if (!grid || grid->numCols != width || grid->numRows != height) {
    if (grid) {
      delete grid;
    }
    grid = new FloatGrid();
    grid->numCols = width;
    grid->numRows = height;
    grid->data = new float*[grid->numRows]();
    if (!grid->data) {
      WARNING_LOGF("TIF file %s too large (out of memory) with %li rows", file, grid->numRows);
      delete grid;
      GTIFFree(gtif);
      XTIFFClose(tif);
      return NULL;
    }
    for (long i = 0; i < grid->numRows; i++) {
      double rowLat = gridBB.top - (float)(i) * pixscale[1];
	if (rowLat >= (tileBB.bottom - pixscale[1]) && rowLat <= (tileBB.top + pixscale[1])) {
      grid->data[i] = new float[grid->numCols]();
      if (!grid->data[i]) {
        WARNING_LOGF("TIF file %s too large (out of memory) with %li columns", file, grid->numCols);
        delete grid;
        GTIFFree(gtif);
        XTIFFClose(tif);
        return NULL;
      }
}
    }
  }
  
  char *noData = NULL;
  if (TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &noData)) {
    grid->noData = atof(noData);
  } else {
    grid->noData = std::numeric_limits<float>::quiet_NaN();
  }
  grid->cellSize = pixscale[0];
  grid->cellSizeX = grid->cellSize;
  grid->cellSizeY = pixscale[1];
  grid->extent.top = gridBB.top;
  grid->extent.left = gridBB.left;
  grid->extent.bottom = gridBB.bottom;
  grid->extent.right = gridBB.right;
 
	GTIFKeyGet(gtif, GTModelTypeGeoKey, &grid->modelType, 0, 1);
  GTIFKeyGet(gtif, GeographicTypeGeoKey, &grid->geographicType, 0, 1);
  GTIFKeyGet(gtif, GeogGeodeticDatumGeoKey, &grid->geodeticDatum, 0, 1);
	grid->geoSet = true;

  if (!TIFFIsTiled(tif)) { 
  for (long i = 0; i < grid->numRows; i++) {
	if (!grid->data[i]) {
		continue;
	}
    if (TIFFReadScanline(tif, grid->data[i], (unsigned int)i, 1) == -1) {
      /*WARNING_LOGF("TIF file %s corrupt? (scanline read failed)", file);
      delete grid;
      GTIFFree(gtif);
      XTIFFClose(tif);
      return NULL;*/
      for (long j = 0; j < grid->numCols; j++) {
        grid->data[i][j] = grid->noData;
      }
    }
  }
  } else {
        int tileSizeFloats = TIFFTileSize(tif) / 4;
        unsigned int tileWidth, tileLength;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileWidth);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileLength);
        float *tileBuf = new float[tileSizeFloats];
        for (unsigned int y = 0; y < height; y += tileLength) {
                for (unsigned int x = 0; x < width; x += tileWidth) {
                        BoundingBox box;
                        box.top = gridBB.top - (float)(y) * pixscale[1];
                        box.bottom = gridBB.top - (float)(y + tileLength) * pixscale[1];
                        box.left = gridBB.left + (float)(x) * pixscale[0];
                        box.right = gridBB.left + (float)(x + tileWidth) * pixscale[0];
                        if (box.Intersects(&tileBB)) {
                                TIFFReadTile(tif, tileBuf, x, y, 0, 0);
                                for (unsigned int j = 0; j < tileLength; j++) {
                                        for (unsigned int i = 0; i < tileWidth; i++) {
                                                unsigned int gy = y + j, gx = x + i;
                                                if (gy >= height || gx >= width || !grid->data[gy]) {
                                                        continue;
                                                }
                                                unsigned int tileIndex = j * tileLength + i;
                                                grid->data[gy][gx] = tileBuf[tileIndex];
                                        }
                                }

                        }
                }
        }
  }
  
  GTIFFree(gtif);
  XTIFFClose(tif);
  
  return grid;
  
}

void WriteFloatTifGrid(const char *file, FloatGrid *grid, const char *artist, const char *datetime, const char *copyright) {
  
  TIFFExtenderInit();
  
  TIFF *tif = NULL;
  GTIF *gtif = NULL;
  
  tif = XTIFFOpen(file, "w");
  if (!tif) {
    return;
  }
  
  gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return;
  }
  
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);
  
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, grid->numCols);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, grid->numRows);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP,  20);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
	char buf[100];
  sprintf(buf, "%f", grid->noData);
  TIFFSetField(tif, TIFFTAG_GDAL_NODATA, buf);
 	sprintf(buf, "Tif2Tile%s", "");
	TIFFSetField(tif, TIFFTAG_SOFTWARE, buf);
	if (artist) {
		TIFFSetField(tif, TIFFTAG_ARTIST, artist);
	}
	if (datetime) {
		TIFFSetField(tif, TIFFTAG_DATETIME, datetime);
	}
	if (copyright) {
		TIFFSetField(tif, TIFFTAG_COPYRIGHT, copyright);
	}
 
  double tiepoints[6];
  double pixscale[3];
  
  pixscale[0] = grid->cellSizeX;
  pixscale[1] = grid->cellSizeY;
  pixscale[2] = 0.0;
  tiepoints[0] = 0;
  tiepoints[1] = 0;
  tiepoints[2] = 0;
  tiepoints[5] = 0;
  tiepoints[4] = grid->extent.top;
  tiepoints[3] = grid->extent.left;
  //grid->extent.bottom = tiepoints[4]-(pixscale[1] * float(height));
  //grid->extent.right = tiepoints[3]+(pixscale[0] * float(width));*/
  
  TIFFSetField(tif, TIFFTAG_GEOTIEPOINTS, 6, tiepoints);
  TIFFSetField(tif, TIFFTAG_GEOPIXELSCALE, 3, pixscale);

	if (grid->geoSet) {
		GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT,  1, grid->modelType);
    GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
    GTIFKeySet(gtif, GeographicTypeGeoKey, TYPE_SHORT, 1, grid->geographicType);
    GTIFKeySet(gtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1, grid->geodeticDatum);
    GTIFKeySet(gtif, GeogAngularUnitsGeoKey, TYPE_SHORT,  1, Angular_Degree);
	} else { 
		GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT,  1, ModelGeographic);
		GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
		GTIFKeySet(gtif, GeographicTypeGeoKey, TYPE_SHORT, 1, GCS_WGS_84);
		GTIFKeySet(gtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1, Datum_WGS84);
		GTIFKeySet(gtif, GeogAngularUnitsGeoKey, TYPE_SHORT,  1, Angular_Degree);
	}
 
  for (long i = 0; i < grid->numRows; i++) {
    if (TIFFWriteScanline(tif, grid->data[i], (unsigned int)i, 0) == -1) {
      printf("eek!\n");
    }
  }

	GTIFWriteKeys(gtif);  
  GTIFFree(gtif);
  XTIFFClose(tif);
  
}

LongGrid *ReadLongTifGrid(const char *file) {
  
  LongGrid *grid = NULL;
  TIFF *tif = NULL;
  GTIF *gtif = NULL;
  
  TIFFSetErrorHandler(NULL);
  
  tif = XTIFFOpen(file, "r");
  if (!tif) {
    return NULL;
  }
  
  gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return NULL;
  }
  
  unsigned short sampleFormat, samplesPerPixel, bitsPerSample;
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  
  if (sampleFormat != SAMPLEFORMAT_INT || bitsPerSample != 32 || samplesPerPixel != 1) {
    WARNING_LOGF("%s is not a supported Int GeoTiff", file);
    GTIFFree(gtif);
    XTIFFClose(tif);
    return NULL;
  }
  
  int width, height;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  
  short tiepointsize, pixscalesize;
  double* tiepoints;//[6];
  double* pixscale;//[3];
  TIFFGetField(tif, TIFFTAG_GEOTIEPOINTS,  &tiepointsize,
               &tiepoints);
  TIFFGetField(tif, TIFFTAG_GEOPIXELSCALE, &pixscalesize,
               &pixscale);
  
  grid = new LongGrid();
  
  grid->numCols = width;
  grid->numRows = height;
  grid->cellSize = pixscale[0];
  grid->extent.top = tiepoints[4];
  grid->extent.left = tiepoints[3];
  grid->extent.bottom = tiepoints[4]-(pixscale[1] * float(height));
  grid->extent.right = tiepoints[3]+(pixscale[0] * float(width));
  
  grid->data = new long*[grid->numRows];
  for (long i = 0; i < grid->numRows; i++) {
    grid->data[i] = new long[grid->numCols];
    if (TIFFReadScanline(tif, grid->data[i], (unsigned int)i, 1) == -1) {
      printf("eek!\n");
    }
  }
  
  GTIFFree(gtif);
  XTIFFClose(tif);
  
  return grid;
  
}
