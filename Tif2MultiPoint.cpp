#include <cstdio>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include "Grid.h"
#include "TifGrid.h"

#define NO_DATA "No Data"

struct Point {
	char name[255];
	char data[255];
	float lat;
	float lon;
};


static float GetDataValue(FloatGrid *grid, double lat, double lon);
static bool ReadPoints(char *file);

std::vector<Point *> points;

int main(int argc, char *argv[]) {

	if (argc < 6) {
		printf("%s inputCSV [geojson or czml] units unitsSI unitsUS outputFile inputTif1...\n", argv[0]);
		return 1;
	}
	
	char *argInputCSV = argv[1];
	char *argFormat = argv[2];
	char *argUnits = argv[3];
	char *argUnitsSI = argv[4];
	char *argUnitsUS = argv[5];
	char *argOutput = argv[6];
	int expectedArgs = 7;
	int numInputFiles = argc - expectedArgs;
	int argInputFileIndex = expectedArgs;

	if (!ReadPoints(argInputCSV)) {
		printf("Point reading failure\n");
		return 1;
	}

	FloatGrid *dataGrids[numInputFiles];
	bool allOutside = true;
	bool foundTifs = false;
	int firstIndex = -1;

	double top = -90.0, bottom = 90.0, left = 180.0, right = -180.0;
	for (size_t i = 0; i < points.size(); i++) {
		if (points[i]->lat > top) {
			top = points[i]->lat;
		}
		if (points[i]->lat < bottom) {
                        bottom = points[i]->lat;
                }
		if (points[i]->lon > right) {
                        right = points[i]->lon;
                }
		if (points[i]->lon < left) {
                        left = points[i]->lon;
                }
	}

	for (int i = 0; i < numInputFiles; i++) {
		bool outside = false;
  		dataGrids[i] = ReadFloatTifGrid(argv[argInputFileIndex + i], top, bottom, left, right, &outside);
		if (dataGrids[i] && !foundTifs) {
			foundTifs = true;
			firstIndex = i;
		}
		if (!dataGrids[i] && !outside) {
			allOutside = false;
		}
	}

	if (!foundTifs && allOutside) {
		printf(NO_DATA);
		return 0;
	} else if (!foundTifs) {
		printf(NO_DATA);
                return 1;
	}
	
	for (size_t i = 0; i < points.size(); i++) {
		float data = dataGrids[firstIndex]->noData;
		for (int j = 0; j < numInputFiles; j++) {
			if (!dataGrids[j]) {
				continue;
			}
			data = GetDataValue(dataGrids[j], points[i]->lat, points[i]->lon);
			if (data != dataGrids[j]->noData) {
				break; // We found some data!!
			}
		}

		if (data == dataGrids[firstIndex]->noData) {
			sprintf(points[i]->data, "%s", NO_DATA);
		} else {
			sprintf(points[i]->data, "%.02f", data);
		}
	}

	FILE *output = fopen(argOutput, "wb");
	if (!strcasecmp(argFormat, "czml")) {
		//CZML output
		fprintf(output, "%s\n", "[{\"id\":\"document\",\"name\":\"Labels\",\"version\":\"1.0\"}");
		for (size_t i = 0; i < points.size(); i++) {
			fprintf(output, ",{\"id\":\"%i\",\"name\":\"%s\",\"description\":\"%s\",\"label\":{\"text\":\"%s\",\"font\":\"14pt Lucida Console\",\"style\":\"FILL_AND_OUTLINE\",\"outlineWidth\":4,\"outlineColor\":{\"rgba\":[0,0,0,255]}},\"heightReference\":\"CLAMP_TO_GROUND\",\"position\":{\"cartographicDegrees\":[\"%f\",\"%f\",0]}}\n", i, points[i]->name, points[i]->data, points[i]->data, points[i]->lat, points[i]->lon);
		}
		fprintf(output, "%s\n", "]");
	} else {
		// geojson output
		fprintf(output, "%s\n", "[");
		for (size_t i = 0; i < points.size(); i++) {
			fprintf(output, "%s{\"lat\": %f, \"lon\": %f, \"text\": \"%s\", \"units\": \"%s\", \"unitssi\": \"%s\",\"unitsus\": \"%s\"}\n", (i != 0) ? "," : "", points[i]->lat, points[i]->lon, points[i]->data, argUnits, argUnitsSI, argUnitsUS);
                }
		fprintf(output, "%s\n", "]");
	}
	fclose(output);
	return 0;
}

bool ReadPoints(char *file) {
	FILE *pFile = fopen(file, "rb");
	if (pFile == NULL) {
		printf("Failed to open file %s\n", file);
		return false;
	}
   	while (!feof(pFile)) {
		Point *pt = new Point; 
		if (fscanf(pFile, "%[^;];%f;%f ", &(pt->name[0]), &(pt->lat), &(pt->lon)) == 3) {
			points.push_back(pt);
		}
	}

	fclose(pFile);

	printf("Read in %lu points\n", points.size());
	return true;
}

float GetDataValue(FloatGrid *grid, double lat, double lon) {
	GridLoc pt;
	if (!grid->GetGridLoc(lon, lat, &pt)) {
		return grid->noData;
	}
	return grid->data[pt.y][pt.x];
}
