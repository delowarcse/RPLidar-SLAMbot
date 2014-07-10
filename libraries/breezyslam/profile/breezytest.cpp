// SinglePositionSLAM params: gives us a nice-size map
static const int MAP_SIZE_PIXELS        = 800;
static const int MAP_SCALE_MM_PER_PIXEL =  35;


static const int SCAN_SIZE 		                = 682;

// Arbitrary
static const int RANDOM_SEED                    = 0xabcd;

// Arbitrary maximum length of line in input logfile
#define MAXLINE 10000

#include <iostream>
#include <vector>
using namespace std;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "Position.hpp"
#include "Laser.hpp"
#include "WheeledRobot.hpp"
#include "Velocities.hpp"
#include "algorithms.hpp"


// Methods to load all data from file ------------------------------------------
// Each line in the file has the format:
//
//  TIMESTAMP  ... Q1  Q1 ... Distances
//  (usec)                    (mm)
//  0          ... 2   3  ... 24 ... 
//  
//where Q1, Q2 are odometry values

static void skiptok(char ** cpp)
{
    *cpp = strtok(NULL, " ");
}

static int nextint(char ** cpp)
{
    skiptok(cpp);
    
    return atoi(*cpp);
}

static void load_data(
    const char * dataset, 
    vector<int *> & scans,
    vector<long *> & odometries)
{
    char filename[256];
    
    sprintf(filename, "%s.dat", dataset);
    printf("Loading data from %s ... \n", filename);
    
    FILE * fp = fopen(filename, "rt");
    
    if (!fp)
    {
        fprintf(stderr, "Failed to open file\n");
        exit(1);
    }
    
    char s[MAXLINE];
    
    while (fgets(s, MAXLINE, fp))
    {
        char * cp = strtok(s, " ");
        
               
        long * odometry = new long [3];
        odometry[0] = atol(cp);
        skiptok(&cp);        
        odometry[1] = nextint(&cp);
        odometry[2] = nextint(&cp);
        
        odometries.push_back(odometry);
        
        // Skip unused fields
        for (int k=0; k<20; ++k)
        {
            skiptok(&cp);
        }
        
        int * scanvals = new int [SCAN_SIZE];
        
        for (int k=0; k<SCAN_SIZE; ++k)
        {
            scanvals[k] = nextint(&cp);
        }
        
        scans.push_back(scanvals);
    }
    
    fclose(fp);    
}

// Class for Hokuyo URG04 LIDAR ------------------------------------------------

class HokuyoURG04 : public Laser
{
    
public:
    
    HokuyoURG04(void): Laser(
        SCAN_SIZE,
        10,          // scanRateHz
        -120,        // angleMinDegrees
        +120,        // angleMaxDegrees
        4000,        // distanceNoDetectionMillimeters
        70,          // detectionMargin
        145)         // offsetMillimeters
    {
    }
};

// Class for MinesRover custom robot -------------------------------------------

class Rover : WheeledRobot
{
    
public:
    
    Rover() : WheeledRobot(
         77,     // wheelRadiusMillimeters
        165)     // halfAxleLengthMillimeters
    {
    }
    
    Velocities computeVelocities(long * odometry, Velocities & velocities)
    {  
        return WheeledRobot::computeVelocities(
            odometry[0], 
            odometry[1], 
            odometry[2]);
    }

protected:    
    
    void extractOdometry(
        double timestamp, 
        double leftWheelOdometry, 
        double rightWheelOdometry, 
        double & timestampSeconds, 
        double & leftWheelDegrees, 
        double & rightWheelDegrees)
    {        
        // Convert microseconds to seconds, ticks to angles        
        timestampSeconds = timestamp / 1e6;
        leftWheelDegrees = ticksToDegrees(leftWheelOdometry);
        rightWheelDegrees = ticksToDegrees(rightWheelOdometry);
    }
    
    void descriptorString(char * str)
    {
        sprintf(str, "ticks_per_cycle=%d", this->TICKS_PER_CYCLE);
    }
        
private:
    
    double ticksToDegrees(double ticks)
    {
        return ticks * (180. / this->TICKS_PER_CYCLE);
    }
    
    static const int TICKS_PER_CYCLE = 2000;
};

// Progress-bar class
// Adapted from http://code.activestate.com/recipes/168639-progress-bar-class/
// Downloaded 12 January 2014

class ProgressBar
{
public:
    
    ProgressBar(int minValue, int maxValue, int totalWidth)
    {
        strcpy(this->progBar, "[]");   // This holds the progress bar string
		this->min = minValue;
		this->max = maxValue;
		this->span = maxValue - minValue;
		this->width = totalWidth;
		this->amount = 0;       // When amount == max, we are 100% done 
		this->updateAmount(0);  // Build progress bar string
	}
	
    void updateAmount(int newAmount)
    {
		if (newAmount < this->min)
		{
		    newAmount = this->min;
		}
		if (newAmount > this->max)
		{
		    newAmount = this->max;
		}
		
		this->amount = newAmount;

		// Figure out the new percent done, round to an integer
		float diffFromMin = float(this->amount - this->min);
		int percentDone = (int)round((diffFromMin / float(this->span)) * 100.0);

		// Figure out how many hash bars the percentage should be
		int allFull = this->width - 2;
		int numHashes = (int)round((percentDone / 100.0) * allFull);

		
		// Build a progress bar with hashes and spaces
		strcpy(this->progBar, "[");
		this->addToProgBar("#", numHashes);
		this->addToProgBar(" ", allFull-numHashes);
		strcat(this->progBar, "]");
		
		// Figure out where to put the percentage, roughly centered
		int percentPlace =  (strlen(this->progBar) / 2) - ((int)(log10(percentDone+1)) + 1);
		char percentString[5];
		sprintf(percentString, "%d%%", percentDone);
				
		// Put it there
		for (int k=0; k<strlen(percentString); ++k)
		{
		    this->progBar[percentPlace+k] = percentString[k];
		}
		
    }
    
    char * str()
    {   
        return this->progBar;
    }
    
private:
    
    char progBar[1000]; // more than we should ever need
    int min;
    int max;
    int span;
    int width;
    int amount;
     
    void addToProgBar(const char * s, int n)
    {
        for (int k=0; k<n; ++k)
        {
            strcat(this->progBar, s);
        }
    }
};

// Helpers ----------------------------------------------------------------

int coords2index(double x,  double y)
{    
    return y * MAP_SIZE_PIXELS + x;
}


int mm2pix(double mm)
{
    return (int)(mm / MAP_SCALE_MM_PER_PIXEL);
}

int main( int argc, const char** argv )
{    
    // Bozo filter for input args
    if (argc < 4)
    {
        fprintf(stderr, 
            "Usage:   %s <dataset> <use_odometry> <use_stochastic_search>\n", 
            argv[0]);
        fprintf(stderr, "Example: %s <exp2> 1 0\n", argv[0]);
        exit(1);
    }
    
    // Grab input args
    const char * dataset = argv[1];
    bool use_odometry        =  atoi(argv[2]) ? true : false;
    bool use_stochastic_search =  atoi(argv[3]) ? true : false;
    
    // Load the Lidar and odometry data from the file   
    vector<int *> scans;
    vector<long *> odometries;
    load_data(dataset, scans, odometries);
       
    // Build a robot model in case we want odometry
    Rover robot = Rover();
    
    // Create a byte array to receive the computed maps
    unsigned char * mapbytes = new unsigned char[MAP_SIZE_PIXELS * MAP_SIZE_PIXELS];
        
    // Create SLAM object
    HokuyoURG04 laser;
    SinglePositionSLAM * slam = use_stochastic_search ?
    (SinglePositionSLAM*)new RMHC_SLAM(laser, MAP_SIZE_PIXELS, MAP_SCALE_MM_PER_PIXEL, RANDOM_SEED) :
    (SinglePositionSLAM*)new Deterministic_SLAM(laser, MAP_SIZE_PIXELS, MAP_SCALE_MM_PER_PIXEL);
	    
    // Report what we're doing
    int nscans = 100; //scans.size();
    printf("Processing %d scans with%s odometry / with%s particle filter...\n",
        nscans, use_odometry ? "" : "out", use_stochastic_search ? "" : "out");
    ProgressBar * progbar = new ProgressBar(0, nscans, 80); 
        
    // Start with an empty trajectory of positions
    vector<double *> trajectory;
    
    // Loop over scans
    for (int scanno=0; scanno<nscans; ++scanno)
    {                         
        int * lidar = scans[scanno];
        
        // Update with/out odometry
        if (use_odometry)
        {
            Velocities velocities = robot.computeVelocities(odometries[scanno], velocities);
            slam->update(lidar, velocities);            
        }
        else
        {
            ((RMHC_SLAM *)slam)->update(lidar);  
        }
        
        Position position = slam->getpos();
                        
        // Add new coordinates to trajectory
        double * v = new double[2];
        v[0] = position.x_mm;
        v[1] = position.y_mm;
        trajectory.push_back(v);     
        
        // Tame impatience
        progbar->updateAmount(scanno);
        printf("\r%s", progbar->str());
        fflush(stdout);
    }

    printf("\n");
              
    // Get final map
    slam->getmap(mapbytes);

    
    // Put trajectory into map as black pixels
    for (int k=0; k<(int)trajectory.size(); ++k)
    {        
        double * v = trajectory[k];
                        
        int x = mm2pix(v[0]);
        int y = mm2pix(v[1]);
        
        delete v;
                        
        mapbytes[coords2index(x, y)] = 0;
    }
            
    delete slam;
    delete progbar;
    delete mapbytes;
    
    return 0;
}