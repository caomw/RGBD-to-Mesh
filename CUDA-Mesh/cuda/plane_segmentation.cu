#include "plane_segmentation.h"


__device__ glm::vec3 normalFrom3x3Covar(glm::mat3 A, glm::vec3& eigs) {
	// Given a (real, symmetric) 3x3 covariance matrix A, returns the eigenvector corresponding to the min eigenvalue
	// (see: http://en.wikipedia.org/wiki/Eigenvalue_algorithm#3.C3.973_matrices)
	glm::vec3 normal = glm::vec3(0.0f);

	float p1 = A[0][1]*A[0][1] + A[0][2]*A[0][2] + A[1][2]*A[1][2];
	if (abs(p1) < 0.00001f) { // A is diagonal
		eigs = glm::vec3(A[0][0], A[1][1], A[2][2]);

		float tmp;
		int i, eig_i;
		// sorting: swap first pair if necessary, then second pair, then first pair again
		for (i=0; i<3; i++) {
			eig_i = i%2;
			tmp = eigs[eig_i];
			eigs[eig_i] = glm::max(tmp, eigs[eig_i+1]);
			eigs[eig_i+1] = glm::min(tmp, eigs[eig_i+1]);
		}
	} else {
		float q = (A[0][0] + A[1][1] + A[2][2])/3.0f; // mean(trace(A))
		float p2 = (A[0][0]-q)*(A[0][0]-q) + (A[1][1]-q)*(A[1][1]-q) + (A[2][2]-q)*(A[2][2]-q)+ 2*p1;
		float p = sqrt(p2/6);
		glm::mat3 B = (1/p) * (A-q*glm::mat3(1.0f));
		float r = glm::determinant(B)/2;
		// theoretically -1 <= r <= 1, but clamp in case of numeric error
		float phi;
		if (r <= -1) {
			phi = PI_F / 3;
		} else if (r >= 1) { 
			phi = 0;
		} else {
			phi = glm::acos(r)/3;
		}
		eigs[0] = q + 2*p*glm::cos(phi);
		eigs[2] = q + 2*p*glm::cos(phi + 2*PI_F/3);
		eigs[1] = 3*q - eigs[0] - eigs[2];

	}



	//N = (A-eye(3)*eig1)*(A(:,1)-[1;0;0]*eig2);
	glm::mat3 Aeig1 = A;
	Aeig1[0][0] -= eigs[0];
	Aeig1[1][1] -= eigs[0];
	Aeig1[2][2] -= eigs[0];
	normal = Aeig1*(A[0] - glm::vec3(eigs[1],0.0f,0.0f));

	float length = glm::length(normal);
	normal /= length;
	return normal;
}


#pragma region Histogram Two-D

__global__ void normalHistogramKernel(float* normX, float* normY, float* normZ, int* finalSegmentsBuffer, int* histogram, 
									  int xRes, int yRes, int xBins, int yBins, bool excludePreviousSegments)
{
	int i = threadIdx.x + blockIdx.x*blockDim.x;

	if( i  < xRes*yRes)
	{
		float x = normX[i];
		float y = normY[i];
		float z = normZ[i];
		bool unsegmented = excludePreviousSegments?(finalSegmentsBuffer[i] == -1):true;

		if(x == x && y == y && z == z && unsegmented)//Will be false if NaN
		{

			//Original Normals are all viewpoint oriented (most will have -z values). 
			//However, since we want to project them down into a unit hemisphere
			//only z values will be allowed. So if z is negative, flip normal
			//Original normals will be 
			if(z < 0.0f)
			{
				x = -x;
				y = -y;
				z = -z;
			}

			//int xI = (x+1.0f)*0.5f*xBins;//x in range of -1 to 1. Map to 0 to 1.0 and multiply by number of bins
			//int yI = (y+1.0f)*0.5f*yBins;//x in range of -1 to 1. Map to 0 to 1.0 and multiply by number of bins
			int xI = acos(x)*PI_INV_F*xBins;
			int yI = acos(y)*PI_INV_F*yBins;

			//Projected space is well behaved w.r.t indexing when 0 <= z <= 1
			//float azimuth = atan2f(z,x);
			//int xI = azimuth*PI_INV_F*xBins;
			//int yI = acosf(y)*PI_INV_F*yBins;

			atomicAdd(&histogram[yI*xBins + xI], 1);
		}
	}
}



__host__ void computeNormalHistogram(float* normX, float* normY, float* normZ, int* finalSegmentsBuffer, int* histogram, 
									 int xRes, int yRes, int xBins, int yBins, bool excludePreviousSegments)
{
	int blockLength = 256;

	dim3 threads(blockLength);
	dim3 blocks((int)(ceil(float(xRes*yRes)/float(blockLength))));


	normalHistogramKernel<<<blocks,threads>>>(normX, normY, normZ, finalSegmentsBuffer, histogram, 
		xRes, yRes, xBins, yBins, excludePreviousSegments);

}

__global__ void clearHistogramKernel(int* histogram, int length)
{
	int i = threadIdx.x+blockIdx.x*blockDim.x;

	if(i < length)
	{
		histogram[i] = 0;
	}
}

__host__ void clearHistogram(int* histogram, int xBins, int yBins)
{
	int blockLength = 256;

	dim3 threads(blockLength);
	dim3 blocks((int)(ceil(float(xBins*yBins)/float(blockLength))));

	clearHistogramKernel<<<blocks,threads>>>(histogram, xBins*yBins);

}

#pragma endregion


#pragma region ACos Histogram One-D

//TODO: Shared memory
__global__ void ACosHistogramKernel(float* cosineValue, int* histogram, int valueCount, int numBins)
{
	extern __shared__ int s_hist[];
	s_hist[threadIdx.x] = 0;
	__syncthreads();

	int valueI = threadIdx.x + blockDim.x * blockIdx.x;

	if(valueI < valueCount)
	{
		float angle = acosf(cosineValue[valueI]);

		if(angle == angle){
			int histIndex = angle*PI_INV_F*numBins;
			if(histIndex >= 0 && histIndex < numBins)//Sanity check
				atomicAdd(&s_hist[histIndex], 1);
		}
	}

	__syncthreads();

	atomicAdd(&histogram[threadIdx.x], s_hist[threadIdx.x]);
}

__host__ void ACosHistogram(float* cosineValue, int* histogram, int valueCount, int numBins)
{
	int blockLength = numBins;

	dim3 threads(blockLength);
	dim3 blocks((int)(ceil(float(valueCount)/float(blockLength))));

	ACosHistogramKernel<<<blocks,threads, numBins*sizeof(int)>>>(cosineValue, histogram, valueCount, numBins);
}

#pragma endregion



#pragma region Histogram Peak Detection Two-D

__device__ int mod_pos (int a, int b)
{
	int ret = a % b;
	if(ret < 0)
		ret+=b;
	return ret;
}

__global__ void normalHistogramPrimaryPeakDetectionKernel(int* histogram, int xBins, int yBins, Float3SOA peaks, int maxPeaks, 
														  int exclusionRadius, int minPeakHeight, float previousPeaksClearRadius)
{	
	extern __shared__ int s_temp[];
	int* s_hist = s_temp;
	int* s_max = s_hist + xBins*yBins;
	int* s_maxI = s_max + (xBins*yBins)/2;

	int index = threadIdx.x + threadIdx.y*xBins;
	//Load histogram
	s_hist[index] = histogram[index];

	for(int p = 0; p < maxPeaks; ++p)
	{
		int px = peaks.x[p];//x index of peak
		int py = peaks.y[p];//y index of peak
		int dx = min(mod_pos((px - threadIdx.x), xBins), mod_pos((threadIdx.x - px),xBins));//shortest path to peak (wraps around)
		int dy = min(mod_pos((py - threadIdx.y), yBins), mod_pos((threadIdx.y - py),yBins));//shortest path to peak (wraps around)


		if(dx*dx+dy*dy <= previousPeaksClearRadius*previousPeaksClearRadius)
		{
			s_hist[index] = 0;
		}
	}

	__syncthreads();


	float totalCount = 0.0f;
	float xPos = 0.0f;
	float yPos = 0.0f;
	for(int x = -1; x <= 1; ++x)
	{
		int tx = threadIdx.x + x;
		for(int y = -1; y <= 1; ++y)
		{
			int ty = threadIdx.y + y;
			int binCount = s_hist[mod_pos(tx, xBins) + mod_pos(ty, yBins)*xBins];//wrap histogram index
			totalCount += binCount;
			xPos += binCount*tx;
			yPos += binCount*ty;

		}

	}

	if(totalCount > 0)
	{
		xPos /= totalCount;
		yPos /= totalCount;
	}

	//Preprocessing complete

	//=========Peak detection Loop===========
	int histLength = xBins*yBins;
	for(int peakNum = 0; peakNum < maxPeaks; ++peakNum)
	{

#pragma region Maximum Finder
		//========Compute maximum=======
		//First step loads from main hist, so do outside loop
		int halfpoint = histLength >> 1;
		int thread2 = index + halfpoint;
		if(index < halfpoint)
		{
			int temp = s_hist[thread2];
			bool leftSmaller = (s_hist[index] < temp);
			s_max[index] = leftSmaller?temp:s_hist[index];
			s_maxI[index] = leftSmaller?thread2:index;
		}
		__syncthreads();
		while(halfpoint > 0)
		{
			halfpoint >>= 1;
			if(index < halfpoint)
			{
				thread2 = index + halfpoint;
				int temp = s_max[thread2];
				if (temp > s_max[index]) {
					s_max[index] = temp;
					s_maxI[index] = s_maxI[thread2];
				}
			}
			__syncthreads();
		}

		//========Compute maximum End=======
#pragma endregion



		//s_maxI[0] now holds the maximum index

		if(s_max[0] < minPeakHeight)
		{
			//Fill remaining slots with NAN
			if(index >= peakNum && index < maxPeaks)
			{
				peaks.x[index] = CUDART_NAN_F;
				peaks.y[index] = CUDART_NAN_F;
				peaks.z[index] = 0;
			}
			break;
		}

		if(s_maxI[0] == index)
		{
			peaks.x[peakNum] = xPos;
			peaks.y[peakNum] = yPos;
			peaks.z[peakNum] = totalCount;
			//DEBUG
			histogram[index] = -(peakNum+1);
		}


		//Distance to max
		int px = (s_maxI[0] % xBins);//x index of peak
		int py = (s_maxI[0] / yBins);//y index of peak
		int dx = min(mod_pos((px - threadIdx.x), xBins), mod_pos((threadIdx.x - px),xBins));//shortest path to peak (wraps around)
		int dy = min(mod_pos((py - threadIdx.y), yBins), mod_pos((threadIdx.y - py),yBins));//shortest path to peak (wraps around)


		if(dx*dx+dy*dy < exclusionRadius*exclusionRadius)
		{
			s_hist[index] = 0;
		}


		__syncthreads();
	}
}


__host__ void normalHistogramPrimaryPeakDetection(int* histogram, int xBins, int yBins, Float3SOA peaks, int maxPeaks, 
												  int exclusionRadius, int minPeakHeight, float previousPeaksClearRadius)
{
	assert(xBins*yBins <= 1024);//For now enforce strict limit. Might be expandable in future, but most efficient like this
	assert(!(xBins*yBins  & (xBins*yBins  - 1))); //Assert is power of two



	dim3 threads(xBins, yBins);
	dim3 blocks(1);

	int sharedMem = xBins*yBins*2*sizeof(int);

	normalHistogramPrimaryPeakDetectionKernel<<<blocks,threads,sharedMem>>>(histogram, xBins, yBins, peaks, 
		maxPeaks, exclusionRadius, minPeakHeight, previousPeaksClearRadius);
}

#pragma endregion

#pragma region Segmentation Two-D

__global__ void segmentNormals2DKernel(Float3SOA rawNormals, Float3SOA rawPositions, 
									   int* normalSegments, float* projectedDistance,
									   int imageWidth, int imageHeight, 
									   int* histogram, int xBins, int yBins, 
									   Float3SOA peaks, int maxPeaks, float maxAngleRange)
{
	extern __shared__ float s_mem[];
	float* s_peaksX = s_mem;
	float* s_peaksY = s_peaksX + maxPeaks;
	float* s_peaksZ = s_peaksY + maxPeaks;

	int index = threadIdx.x + blockIdx.x*blockDim.x;

	if(threadIdx.x < maxPeaks)
	{
		float xi = peaks.x[threadIdx.x];
		float yi = peaks.y[threadIdx.x];
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		if(xi == xi && yi == yi){
			//x = xi/float(xBins)*2.0f - 1.0f;
			//y = yi/float(yBins)*2.0f - 1.0f;

			x = cosf(xi*PI_F/xBins);
			y = cosf(yi*PI_F/yBins);
			z = sqrt(1-x*x-y*y);
		}

		s_peaksX[threadIdx.x] = x;
		s_peaksY[threadIdx.x] = y;
		s_peaksZ[threadIdx.x] = z;
	}

	__syncthreads();


	if(index < imageWidth*imageHeight)
	{

		glm::vec3 normal = glm::vec3(rawNormals.x[index], rawNormals.y[index], rawNormals.z[index]);
		int bestPeak = -1;
		if(normal.x == normal.x && normal.y == normal.y && normal.z == normal.z)
		{
			//normal is valid
			for(int peakNum = 0; peakNum < maxPeaks; ++peakNum)
			{
				float dotprod = normal.x*s_peaksX[peakNum] + normal.y*s_peaksY[peakNum] + normal.z*s_peaksZ[peakNum];
				float angle = acosf(abs(dotprod));

				if(angle < maxAngleRange)
				{
					bestPeak = peakNum;
					break;
				}
			}
		}

		float projectedD = CUDART_NAN_F;//Initialize to NAN
		if(bestPeak >= 0)
		{
			//Peak found, compute projection
			projectedD = abs(s_peaksX[bestPeak]*rawPositions.x[index] 
			+ s_peaksY[bestPeak]*rawPositions.y[index] 
			+ s_peaksZ[bestPeak]*rawPositions.z[index]);

		}


		//Writeback
		normalSegments[index] = bestPeak;
		projectedDistance[index] = projectedD;
	}

}

__host__ void segmentNormals2D(Float3SOA rawNormals, Float3SOA rawPositions, 
							   int* normalSegments, float* projectedDistance,int imageWidth, int imageHeight,
							   int* normalHistogram, int xBins, int yBins, 
							   Float3SOA peaks, int maxPeaks, float maxAngleRange)
{
	int blockLength = 512;
	assert(blockLength > maxPeaks);

	dim3 blocks((int) ceil(float(imageWidth*imageHeight)/float(blockLength)));
	dim3 threads(blockLength);

	int sharedCount = sizeof(float)*(3 * maxPeaks);

	segmentNormals2DKernel<<<blocks, threads, sharedCount>>>(rawNormals, rawPositions, normalSegments, projectedDistance, 
		imageWidth, imageHeight, normalHistogram, xBins, yBins, peaks, maxPeaks, maxAngleRange);
}


#pragma endregion

#pragma region Distance Histograms

__global__ void distanceHistogramKernel(int* dev_normalSegments, float* dev_planeProjectedDistanceMap, int xRes, int yRes,
										int* dev_distanceHistograms, int numMaxNormalSegments, 
										int histcount, float histMinDist, float histMaxDist)
{
	extern __shared__ int s_temp[];
	int* s_hist = s_temp;

	int index = threadIdx.x + blockIdx.x*blockDim.x;

	int segment = -1;
	float dist = CUDART_NAN_F;
	if(index < xRes*yRes){
		segment = dev_normalSegments[index];
		dist = dev_planeProjectedDistanceMap[index];
	}

	int histI = -1;
	if(segment >= 0)
	{
		if(dist < histMaxDist && dist >= histMinDist) 
			histI = (dist - histMinDist)*histcount/(histMaxDist-histMinDist);
	}

	//Each thread has locally stored values.
	for(int peak = 0; peak < numMaxNormalSegments; ++peak)
	{
		//reset histogram
		s_temp[threadIdx.x] = 0;
		__syncthreads();

		if(segment == peak && histI >= 0)
		{
			atomicAdd(&s_hist[histI], 1);
		}

		__syncthreads();

		atomicAdd(&(dev_distanceHistograms[peak*histcount + threadIdx.x]), s_hist[threadIdx.x]);
	}
}

__host__ void generateDistanceHistograms(int* dev_normalSegments, float* dev_planeProjectedDistanceMap, int xRes, int yRes,
										 int** dev_distanceHistograms, int numMaxNormalSegments, 
										 int histcount, float histMinDist, float histMaxDist)
{
	int blockLength = histcount;


	dim3 threads(blockLength);
	dim3 blocks((int)(ceil(float(xRes*yRes)/float(blockLength))));

	int sharedSize = histcount * sizeof(int);

	distanceHistogramKernel<<<blocks,threads,sharedSize>>>(dev_normalSegments, dev_planeProjectedDistanceMap, xRes, yRes, 
		dev_distanceHistograms[0], numMaxNormalSegments, histcount, histMinDist, histMaxDist);
}



__global__ void distHistogramPeakDetectionKernel(int* histogram, int length, int numHistograms, float* distPeaks, int maxDistPeaks, 
												 int exclusionRadius, int minPeakHeight, float minHistDist, float maxHistDist)
{	
	extern __shared__ int s_temp[];
	int* s_hist = s_temp;
	int* s_max = s_hist + length;
	int* s_maxI = s_max + (length)/2;

	int index = threadIdx.x;
	int histOffset = blockIdx.x*length;
	int peaksOffset = blockIdx.x*maxDistPeaks;
	//Load histogram
	s_hist[index] = histogram[index+histOffset];
	__syncthreads();

	float dist = (index*(maxHistDist-minHistDist)/float(length)) + minHistDist;

	//=========Peak detection Loop===========
	for(int peakNum = 0; peakNum < maxDistPeaks; ++peakNum)
	{

#pragma region Maximum Finder
		//========Compute maximum=======
		//First step loads from main hist, so do outside loop
		int halfpoint = length >> 1;
		int thread2 = index + halfpoint;
		if(index < halfpoint)
		{
			int temp = s_hist[thread2];
			bool leftSmaller = (s_hist[index] < temp);
			s_max[index] = leftSmaller?temp:s_hist[index];
			s_maxI[index] = leftSmaller?thread2:index;
		}
		__syncthreads();
		while(halfpoint > 0)
		{
			halfpoint >>= 1;
			if(index < halfpoint)
			{
				thread2 = index + halfpoint;
				int temp = s_max[thread2];
				if (temp > s_max[index]) {
					s_max[index] = temp;
					s_maxI[index] = s_maxI[thread2];
				}
			}
			__syncthreads();
		}

		//========Compute maximum End=======
#pragma endregion



		//s_maxI[0] now holds the maximum index
		if(s_max[0] < minPeakHeight)
		{
			//Fill remaining slots with NaN
			if(index >= peakNum && index < maxDistPeaks)
			{
				distPeaks[peaksOffset + index] = CUDART_NAN_F;
			}
			break;
		}

		if(s_maxI[0] == index)
		{
			distPeaks[peaksOffset + peakNum] = dist;
		}

		//Distance to max
		int dx = s_maxI[0] - threadIdx.x;

		if(abs(dx) <= exclusionRadius)
		{
			s_hist[index] = 0;
		}


		__syncthreads();
	}
}


__host__ void distanceHistogramPrimaryPeakDetection(int* histogram, int length, int numHistograms, float* distPeaks, int maxDistPeaks, 
													int exclusionRadius, int minPeakHeight, float minHistDist, float maxHistDist)
{
	assert(length <= 1024);//For now enforce strict limit. Might be expandable in future, but most efficient like this
	assert(!(length  & (length  - 1))); //Assert is power of two



	dim3 threads(length);
	dim3 blocks(numHistograms);

	int sharedMem = length*2*sizeof(int);

	distHistogramPeakDetectionKernel<<<blocks,threads,sharedMem>>>(histogram, length, numHistograms, 
		distPeaks, maxDistPeaks, exclusionRadius, minPeakHeight, minHistDist, maxHistDist);
}


#pragma endregion


#pragma region Distance Segmentation

__global__ void fineDistanceSegmentationKernel(float* distPeaks, int numNormalPeaks, int maxDistPeaks, 
											   Float3SOA positions, PlaneStats* planeStats,
											   int* normalSegments, float* planeProjectedDistanceMap, 
											   int xRes, int yRes, float maxDistTolerance, int iteration)
{
	//Assemble
	extern __shared__ float s_mem[];
	float* s_distPeaks = s_mem;
	float* s_counts = s_distPeaks + maxDistPeaks*numNormalPeaks;
	float* s_centroidX = s_counts    + maxDistPeaks*numNormalPeaks;
	float* s_centroidY = s_centroidX + maxDistPeaks*numNormalPeaks;
	float* s_centroidZ = s_centroidY + maxDistPeaks*numNormalPeaks;
	float* s_Sxx	= s_centroidZ + maxDistPeaks*numNormalPeaks;
	float* s_Syy	= s_Sxx + maxDistPeaks*numNormalPeaks;
	float* s_Szz	= s_Syy + maxDistPeaks*numNormalPeaks;
	float* s_Sxy	= s_Szz + maxDistPeaks*numNormalPeaks;
	float* s_Syz	= s_Sxy + maxDistPeaks*numNormalPeaks;
	float* s_Sxz	= s_Syz + maxDistPeaks*numNormalPeaks;

	int index = threadIdx.x + blockIdx.x*blockDim.x;
	int planeOffset = iteration*numNormalPeaks*maxDistPeaks;

	//Zero out shared memory
	if(threadIdx.x < numNormalPeaks*maxDistPeaks*(1+3+6+1))
	{
		s_mem[threadIdx.x] = 0.0f;
	}

	if(threadIdx.x < numNormalPeaks*maxDistPeaks)
	{
		s_distPeaks[threadIdx.x] = distPeaks[threadIdx.x];
	}
	__syncthreads();


	if(index < xRes*yRes)
	{
		int normalSeg = normalSegments[index];
		if(normalSeg >= 0)
		{

			float planeD = abs(planeProjectedDistanceMap[index]);
			float px = positions.x[index];
			float py = positions.y[index];
			float pz = positions.z[index];

			//Has a normal segment assignment
			int bestPlaneIndex = -1;
			for(int distPeak = 0; distPeak < maxDistPeaks; ++distPeak)
			{
				int planeIndex = normalSeg*maxDistPeaks + distPeak;
				if(abs(s_distPeaks[planeIndex] - planeD) < maxDistTolerance)
				{
					bestPlaneIndex = planeIndex;
					break;
				}
			}

			if(bestPlaneIndex >= 0)
			{
				//Found a match. Compute stats
				atomicAdd(&s_counts[bestPlaneIndex], 1);//Add one
				atomicAdd(&s_centroidX[bestPlaneIndex], px);
				atomicAdd(&s_centroidY[bestPlaneIndex], py);
				atomicAdd(&s_centroidZ[bestPlaneIndex], pz);
				atomicAdd(&s_Sxx[bestPlaneIndex], px*px);
				atomicAdd(&s_Syy[bestPlaneIndex], py*py);
				atomicAdd(&s_Szz[bestPlaneIndex], pz*pz);
				atomicAdd(&s_Sxy[bestPlaneIndex], px*py);
				atomicAdd(&s_Syz[bestPlaneIndex], py*pz);
				atomicAdd(&s_Sxz[bestPlaneIndex], px*pz);
			}

			normalSegments[index] = bestPlaneIndex;
		}
	}

	__syncthreads();

	if(threadIdx.x < numNormalPeaks*maxDistPeaks)
	{
		atomicAdd(&planeStats[threadIdx.x+planeOffset].count, s_counts[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].centroid.x, s_centroidX[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].centroid.y, s_centroidY[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].centroid.z, s_centroidZ[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Sxx, s_Sxx[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Syy, s_Syy[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Szz, s_Szz[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Sxy, s_Sxy[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Syz, s_Syz[threadIdx.x]);
		atomicAdd(&planeStats[threadIdx.x+planeOffset].Sxz, s_Sxz[threadIdx.x]);
	}
}

__host__ void fineDistanceSegmentation(float* distPeaks, int numNormalPeaks,  int maxDistPeaks, 
									   Float3SOA positions, PlaneStats* planeStats,
									   int* normalSegments, float* planeProjectedDistanceMap, 
									   int xRes, int yRes, float maxDistTolerance, int iteration)
{

	//Stats accum buffers
	//3x float centroid
	//6x float Decoupled S matrix
	//1x float count
	//1x peak distances
	int sharedCount = maxDistPeaks*numNormalPeaks*(3 + 6 + 1 + 1);
	int blockLength = 512;
	assert(blockLength > sharedCount);

	dim3 blocks((int) ceil(float(xRes*yRes)/float(blockLength)));
	dim3 threads(blockLength);


	fineDistanceSegmentationKernel<<<blocks, threads, sizeof(float)*sharedCount>>>(distPeaks, numNormalPeaks, maxDistPeaks, 
		positions, planeStats, normalSegments, planeProjectedDistanceMap, xRes, yRes, maxDistTolerance, iteration);
}


__global__ void clearPlaneStatsKernel(PlaneStats* planeStats, int numNormalPeaks, int numDistPeaks, int iteration)
{
	int index = threadIdx.x + threadIdx.y*numDistPeaks + iteration*(numNormalPeaks*numDistPeaks);

	planeStats[index].count= 0.0f;
	planeStats[index].centroid.x = 0.0f;
	planeStats[index].centroid.y = 0.0f;
	planeStats[index].centroid.z = 0.0f;
	planeStats[index].norm.x = 0.0f;
	planeStats[index].norm.y = 0.0f;
	planeStats[index].norm.z = 0.0f;
	planeStats[index].tangent.x = 0.0f;
	planeStats[index].tangent.y = 0.0f;
	planeStats[index].tangent.z = 0.0f;
	planeStats[index].Sxx = 0.0f;
	planeStats[index].Syy = 0.0f;
	planeStats[index].Szz = 0.0f;
	planeStats[index].Sxy = 0.0f;
	planeStats[index].Syz = 0.0f;
	planeStats[index].Sxz = 0.0f;

}

__host__ void clearPlaneStats(PlaneStats* planeStats, int numNormalPeaks, int numDistPeaks, int maxRounds, int iteration)
{
	assert(numNormalPeaks*numDistPeaks < 1024);
	dim3 threads(numDistPeaks, numNormalPeaks);
	dim3 blocks(1);

	if(iteration < 0)
	{
		for(int i = 0; i < maxRounds; i++)
			clearPlaneStatsKernel<<<blocks,threads>>>(planeStats, numNormalPeaks, numDistPeaks, i);

	}else{
		clearPlaneStatsKernel<<<blocks,threads>>>(planeStats, numNormalPeaks, numDistPeaks, iteration);
	}
}

__device__ glm::mat3 outerProduct(float x, float y, float z)
{
	return glm::mat3(x*x, x*y, x*z,
		y*x, y*y, y*z,
		z*x, z*y, z*z);
}

__device__ bool mergePlanes(int si, int ti, float* s_counts,
							float* s_NormalX, float* s_NormalY, float* s_NormalZ,
							float* s_centroidX, float* s_centroidY, float* s_centroidZ,
							float* s_Sxx, float* s_Syy, float* s_Szz, float* s_Sxy, float* s_Syz, float* s_Sxz,
							float* s_Eig1, float* s_Eig2, float* s_Eig3,
							float mergeAngleThreshCos, float mergeDistThresh)
{
	//Two valid planes. Check merging criteria
	float angleDot = abs(s_NormalX[si]*s_NormalX[ti] + s_NormalY[si]*s_NormalY[ti] + s_NormalZ[si]*s_NormalZ[ti]);

	if(angleDot > mergeAngleThreshCos)
	{
		//Angle match. Check distance
		glm::vec3 centroidDist = glm::vec3(s_centroidX[si] - s_centroidX[ti],
			s_centroidY[si] - s_centroidY[ti],
			s_centroidZ[si] - s_centroidZ[ti]);

		float d1 = abs(s_NormalX[si]*centroidDist.x + s_NormalY[si]*centroidDist.y + s_NormalZ[si]*centroidDist.z);
		float d2 = abs(s_NormalX[ti]*centroidDist.x + s_NormalY[ti]*centroidDist.y + s_NormalZ[ti]*centroidDist.z);
		float delt = glm::min(d1, d2);
		if(delt < mergeDistThresh)
		{
			//============Merge Planes==============
			//Combine counts
			float count_m = s_counts[si] + s_counts[ti];
			//Weighted centroid average
			glm::vec3 mergedCentroid = 1.0f/count_m * 
				glm::vec3(s_counts[si]*s_centroidX[si] + s_counts[ti]*s_centroidX[ti],
				s_counts[si]*s_centroidY[si] + s_counts[ti]*s_centroidY[ti],
				s_counts[si]*s_centroidZ[si] + s_counts[ti]*s_centroidZ[ti]);
			//Merged S2 centroid component
			glm::mat3 Sm2 = outerProduct(mergedCentroid.x, mergedCentroid.y, mergedCentroid.z);

			//Construct S1 for both planes
			glm::mat3 S1_ns = glm::mat3(glm::vec3(s_Sxx[si], s_Sxy[si], s_Sxz[si]), 
				glm::vec3(s_Sxy[si], s_Syy[si], s_Syz[si]),
				glm::vec3(s_Sxz[si], s_Syz[si], s_Szz[si]));

			glm::mat3 S1_nt = glm::mat3(glm::vec3(s_Sxx[ti], s_Sxy[ti], s_Sxz[ti]), 
				glm::vec3(s_Sxy[ti], s_Syy[ti], s_Syz[ti]),
				glm::vec3(s_Sxz[ti], s_Syz[ti], s_Szz[ti]));


			//Compute merged S1_n
			glm::mat3 Sm1_n = s_counts[si]/count_m * S1_ns + s_counts[ti]/count_m * S1_nt;

			//Compute combined normalized scatter matrix and find normals
			glm::vec3 eigs;
			glm::vec3 norm  = normalFrom3x3Covar(Sm1_n - Sm2, eigs);

			//Flip normal towards viewpoint
			//if n dot p > 0, flip towards viewpoint
			if(norm.x*mergedCentroid.x + norm.y*mergedCentroid.y + norm.z * mergedCentroid.z > 0.0f)
			{
				//Flip towards camera
				norm = -norm;
			}

			//=====MERGE DONE, WRITEBACK=====
			s_counts[si] = count_m;
			s_counts[ti] = 0.0f;

			s_centroidX[si] = mergedCentroid.x;
			s_centroidY[si] = mergedCentroid.y;
			s_centroidZ[si] = mergedCentroid.z;

			s_centroidX[ti] = 0.0f;
			s_centroidY[ti] = 0.0f;
			s_centroidZ[ti] = 0.0f;

			s_Sxx[si] = Sm1_n[0][0];
			s_Syy[si] = Sm1_n[1][1];
			s_Szz[si] = Sm1_n[2][2];
			s_Sxy[si] = Sm1_n[0][1];
			s_Syz[si] = Sm1_n[1][2];
			s_Sxz[si] = Sm1_n[0][2];

			s_Sxx[ti] = 0.0f;
			s_Syy[ti] = 0.0f;
			s_Szz[ti] = 0.0f;
			s_Sxy[ti] = 0.0f;
			s_Syz[ti] = 0.0f;
			s_Sxz[ti] = 0.0f;

			s_NormalX[si] = norm.x;
			s_NormalY[si] = norm.y;
			s_NormalZ[si] = norm.z;
			s_Eig1[si] = eigs.x;//Largest
			s_Eig2[si] = eigs.y;//
			s_Eig3[si] = eigs.z;//Smallest

			s_NormalX[ti] = 0.0f;
			s_NormalY[ti] = 0.0f;
			s_NormalZ[ti] = 0.0f;
			s_Eig1[ti] = 0.0f;//Largest
			s_Eig2[ti] = 0.0f;//
			s_Eig3[ti] = 0.0f;//Smallest

			//============End Merge Planes==========
			return true;
		}
	}
	return false;
}


__global__ void finalizePlanesKernel(PlaneStats* planeStats, int numNormalPeaks, int numDistPeaks, 
									 float mergeAngleThreshCos, float mergeDistThresh, int iteration)
{

	extern __shared__ float s_mem[];
	float* s_counts = s_mem;
	float* s_centroidX = s_counts    + numDistPeaks*numNormalPeaks;
	float* s_centroidY = s_centroidX + numDistPeaks*numNormalPeaks;
	float* s_centroidZ = s_centroidY + numDistPeaks*numNormalPeaks;
	float* s_NormalX = s_centroidZ    + numDistPeaks*numNormalPeaks;
	float* s_NormalY = s_NormalX + numDistPeaks*numNormalPeaks;
	float* s_NormalZ = s_NormalY + numDistPeaks*numNormalPeaks;
	float* s_Eig1 = s_NormalZ    + numDistPeaks*numNormalPeaks;
	float* s_Eig2 = s_Eig1 + numDistPeaks*numNormalPeaks;
	float* s_Eig3 = s_Eig2 + numDistPeaks*numNormalPeaks;
	float* s_Sxx	= s_Eig3 + numDistPeaks*numNormalPeaks;
	float* s_Syy	= s_Sxx + numDistPeaks*numNormalPeaks;
	float* s_Szz	= s_Syy + numDistPeaks*numNormalPeaks;
	float* s_Sxy	= s_Szz + numDistPeaks*numNormalPeaks;
	float* s_Syz	= s_Sxy + numDistPeaks*numNormalPeaks;
	float* s_Sxz	= s_Syz + numDistPeaks*numNormalPeaks;

	//Now that all these pointers have been initialized....
	//Load shared memory
	int index = threadIdx.x + threadIdx.y*numDistPeaks;
	int planeOffset = iteration*numDistPeaks*numNormalPeaks;

	int count = planeStats[index+planeOffset].count;
	s_counts[index] = count;

	s_centroidX[index] = planeStats[index+planeOffset].centroid.x/count;
	s_centroidY[index] = planeStats[index+planeOffset].centroid.y/count;
	s_centroidZ[index] = planeStats[index+planeOffset].centroid.z/count;

	//Normalize scatter matrix
	s_Sxx[index] = planeStats[index+planeOffset].Sxx/count;
	s_Syy[index] = planeStats[index+planeOffset].Syy/count;
	s_Szz[index] = planeStats[index+planeOffset].Szz/count;
	s_Sxy[index] = planeStats[index+planeOffset].Sxy/count;
	s_Syz[index] = planeStats[index+planeOffset].Syz/count;
	s_Sxz[index] = planeStats[index+planeOffset].Sxz/count;

	glm::mat3 S1_n = glm::mat3(glm::vec3(s_Sxx[index], s_Sxy[index], s_Sxz[index]), 
		glm::vec3(s_Sxy[index], s_Syy[index], s_Syz[index]),
		glm::vec3(s_Sxz[index], s_Syz[index], s_Szz[index]));

	glm::mat3 S2 = outerProduct(s_centroidX[index], s_centroidY[index], s_centroidZ[index]);

	glm::vec3 eigs;

	glm::vec3 norm = normalFrom3x3Covar(S1_n - S2, eigs);//S1_n is normalized

	//Flip normal towards viewpoint
	//if n dot p > 0, flip towards viewpoint
	if(norm.x*s_centroidX[index] + norm.y*s_centroidY[index] + norm.z * s_centroidZ[index] > 0.0f)
	{
		//Flip towards camera
		norm = -norm;
	}

	s_NormalX[index] = norm.x;
	s_NormalY[index] = norm.y;
	s_NormalZ[index] = norm.z;
	s_Eig1[index] = eigs.x;//Largest
	s_Eig2[index] = eigs.y;//
	s_Eig3[index] = eigs.z;//Smallest

	__syncthreads();

	//Individual planes calculated, do merging now.
	for(int startPeak = 0; startPeak < numDistPeaks; ++startPeak)
	{
		int si = threadIdx.y*numDistPeaks + startPeak;
		if(s_counts[si] > 0)
		{
			for(int testPeak = startPeak + 1; testPeak < numDistPeaks; ++testPeak)
			{
				int ti = threadIdx.y*numDistPeaks + testPeak;
				if(s_counts[ti] > 0)
				{
					mergePlanes(si, ti, s_counts,
						s_NormalX, s_NormalY, s_NormalZ,
						s_centroidX, s_centroidY, s_centroidZ,
						s_Sxx, s_Syy, s_Szz, s_Sxy, s_Syz, s_Sxz,
						s_Eig1, s_Eig2, s_Eig3,
						mergeAngleThreshCos, mergeDistThresh);
				}
			}
		}
	}


	//=======Save final planes (WRITEBACK)======
	planeStats[index+planeOffset].count = s_counts[index];

	planeStats[index+planeOffset].centroid.x = s_centroidX[index];
	planeStats[index+planeOffset].centroid.y = s_centroidY[index];
	planeStats[index+planeOffset].centroid.z = s_centroidZ[index];
	planeStats[index+planeOffset].norm.x = s_NormalX[index];
	planeStats[index+planeOffset].norm.y = s_NormalY[index];
	planeStats[index+planeOffset].norm.z = s_NormalZ[index];
	planeStats[index+planeOffset].eigs.x = s_Eig1[index];
	planeStats[index+planeOffset].eigs.y = s_Eig2[index];
	planeStats[index+planeOffset].eigs.z = s_Eig3[index];
	planeStats[index+planeOffset].Sxx = s_Sxx[index];
	planeStats[index+planeOffset].Syy = s_Syy[index];
	planeStats[index+planeOffset].Szz = s_Szz[index];
	planeStats[index+planeOffset].Sxy = s_Sxy[index];
	planeStats[index+planeOffset].Syz = s_Syz[index];
	planeStats[index+planeOffset].Sxz = s_Sxz[index];

}

__host__ void finalizePlanes(PlaneStats* planeStats, int numNormalPeaks, int numDistPeaks, 
							 float mergeAngleThresh, float mergeDistThresh, int iteration)
{
	assert(numNormalPeaks*numDistPeaks < 1024);
	dim3 threads(numDistPeaks, numNormalPeaks);
	dim3 blocks(1);
	int sharedCount = numDistPeaks*numNormalPeaks*(1 + 3 + 3 + 3 + 6);

	finalizePlanesKernel<<<blocks,threads, sharedCount*sizeof(float)>>>(planeStats, numNormalPeaks, numDistPeaks, 
		cos(mergeAngleThresh), mergeDistThresh, iteration);
}


__global__ void mergePlanesKernel(PlaneStats* planeStats, int numPlanes, float mergeAngleThreshCos, float mergeDistThresh)
{


	extern __shared__ float s_mem[];
	float* s_counts = s_mem;
	float* s_centroidX = s_counts    + numPlanes;
	float* s_centroidY = s_centroidX + numPlanes;
	float* s_centroidZ = s_centroidY + numPlanes;
	float* s_NormalX = s_centroidZ    + numPlanes;
	float* s_NormalY = s_NormalX + numPlanes;
	float* s_NormalZ = s_NormalY + numPlanes;
	float* s_Eig1 = s_NormalZ    + numPlanes;
	float* s_Eig2 = s_Eig1 + numPlanes;
	float* s_Eig3 = s_Eig2 + numPlanes;
	float* s_Sxx	= s_Eig3 + numPlanes;
	float* s_Syy	= s_Sxx + numPlanes;
	float* s_Szz	= s_Syy + numPlanes;
	float* s_Sxy	= s_Szz + numPlanes;
	float* s_Syz	= s_Sxy + numPlanes;
	float* s_Sxz	= s_Syz + numPlanes;

	//Now that all these pointers have been initialized....
	//Load shared memory
	int index = threadIdx.x;

	s_counts[index] = planeStats[index].count;

	s_centroidX[index] = planeStats[index].centroid.x;
	s_centroidY[index] = planeStats[index].centroid.y;
	s_centroidZ[index] = planeStats[index].centroid.z;

	//Normalize scatter matrix
	s_Sxx[index] = planeStats[index].Sxx;
	s_Syy[index] = planeStats[index].Syy;
	s_Szz[index] = planeStats[index].Szz;
	s_Sxy[index] = planeStats[index].Sxy;
	s_Syz[index] = planeStats[index].Syz;
	s_Sxz[index] = planeStats[index].Sxz;

	s_Eig1[index] = planeStats[index].eigs.x;
	s_Eig2[index] = planeStats[index].eigs.y;
	s_Eig3[index] = planeStats[index].eigs.z;

	s_NormalX[index] =planeStats[index].norm.x;
	s_NormalY[index] =planeStats[index].norm.y;
	s_NormalZ[index] =planeStats[index].norm.z;


	__syncthreads();

	if(threadIdx.x == 0)
	{
		//Individual planes calculated, do merging now.
		for(int si = 0; si < numPlanes; ++si)
		{
			if(s_counts[si] > 0)
			{
				for(int ti = si + 1; ti < numPlanes; ++ti)
				{
					if(s_counts[ti] > 0)
					{
						mergePlanes(si, ti, s_counts,
							s_NormalX, s_NormalY, s_NormalZ,
							s_centroidX, s_centroidY, s_centroidZ,
							s_Sxx, s_Syy, s_Szz, s_Sxy, s_Syz, s_Sxz,
							s_Eig1, s_Eig2, s_Eig3,
							mergeAngleThreshCos, mergeDistThresh);
					}
				}
			}
		}
	}

	__syncthreads();


	//=======Save final planes (WRITEBACK)======
	planeStats[index].count= s_counts[index];

	planeStats[index].centroid.x = s_centroidX[index];
	planeStats[index].centroid.y = s_centroidY[index];
	planeStats[index].centroid.z = s_centroidZ[index];
	planeStats[index].norm.x = s_NormalX[index];
	planeStats[index].norm.y = s_NormalY[index];
	planeStats[index].norm.z = s_NormalZ[index];
	planeStats[index].eigs.x = s_Eig1[index];
	planeStats[index].eigs.y = s_Eig2[index];
	planeStats[index].eigs.z = s_Eig3[index];
	planeStats[index].Sxx = s_Sxx[index];
	planeStats[index].Syy = s_Syy[index];
	planeStats[index].Szz = s_Szz[index];
	planeStats[index].Sxy = s_Sxy[index];
	planeStats[index].Syz = s_Syz[index];
	planeStats[index].Sxz = s_Sxz[index];
}


__host__ void mergePlanes(PlaneStats* planeStats, int numPlanes, float mergeAngleThresh, float mergeDistThresh)
{

	assert(numPlanes < 1024);
	dim3 threads(numPlanes);
	dim3 blocks(1);
	int sharedCount = numPlanes*(1 + 3 + 3 + 3 + 6);

	mergePlanesKernel<<<blocks,threads, sharedCount*sizeof(float)>>>(planeStats, numPlanes, cos(mergeAngleThresh), mergeDistThresh);
}


__global__ void fitFinalPlanesKernel(PlaneStats* planeStats, int numPlanes, 
									 Float3SOA norms, Float3SOA positions, int* finalSegmentsBuffer, float* distToPlaneBuffer, 
									 int xRes, int yRes,
									 float fitAngleThreshCos, float fitDistThresh, int iteration)
{
	extern __shared__ float s_mem[];
	float* s_normX = s_mem;
	float* s_normY = s_normX + numPlanes;
	float* s_normZ = s_normY + numPlanes;
	float* s_dist  = s_normZ + numPlanes;

	int planeOffset = iteration*numPlanes;
	if(threadIdx.x < numPlanes)
	{
		int count = planeStats[threadIdx.x + planeOffset].count;
		float validityMultiplier = (count > 0)?1.0f:CUDART_NAN_F;

		s_normX[threadIdx.x] = validityMultiplier*planeStats[threadIdx.x + planeOffset].norm.x;
		s_normY[threadIdx.x] = validityMultiplier*planeStats[threadIdx.x + planeOffset].norm.y;
		s_normZ[threadIdx.x] = validityMultiplier*planeStats[threadIdx.x + planeOffset].norm.z;

		float cx = planeStats[threadIdx.x + planeOffset].centroid.x;
		float cy = planeStats[threadIdx.x + planeOffset].centroid.y;
		float cz = planeStats[threadIdx.x + planeOffset].centroid.z;

		//n dot c = planar offset
		s_dist[threadIdx.x] = validityMultiplier*abs(cx*s_normX[threadIdx.x] + cy*s_normY[threadIdx.x] + cz*s_normZ[threadIdx.x]);

	}

	__syncthreads();

	int index = threadIdx.x + blockIdx.x*blockDim.x;

	float minDist = 1000000.0f;
	float bestPlane = -1;
	if(iteration > 0)
	{
		//If not first iteration, load previous values from segment buffer
		bestPlane = finalSegmentsBuffer[index];
		minDist = distToPlaneBuffer[index];
	}

	float nx = norms.x[index];
	float ny = norms.y[index];
	float nz = norms.z[index];
	float px = positions.x[index];
	float py = positions.y[index];
	float pz = positions.z[index];

	for(int plane = 0; plane < numPlanes; ++plane)
	{
		if(s_dist[plane] == s_dist[plane])//Skip non-valid planes
		{
			float dotprod = abs(nx*s_normX[plane] + ny*s_normY[plane] + nz*s_normZ[plane]);
			if(dotprod > fitAngleThreshCos)
			{
				float dist = abs(px*s_normX[plane] + py*s_normY[plane] + pz*s_normZ[plane]);
				dist = abs(dist - s_dist[plane]);
				if(dist < fitDistThresh && dist < minDist)
				{
					minDist = dist;
					bestPlane = plane + iteration*numPlanes;
				}
			}
		}
	}



	//WRITEBACK
	finalSegmentsBuffer[index] = bestPlane;
	distToPlaneBuffer[index] = minDist;

}


__host__ void fitFinalPlanes(PlaneStats* planeStats, int numPlanes, 
							 Float3SOA norms, Float3SOA positions, int* finalSegmentsBuffer, float* distToPlaneBuffer, int xRes, int yRes,
							 float fitAngleThresh, float fitDistThresh, int iteration)
{
	int blockLength = 512;
	assert(blockLength > numPlanes);
	int sharedCount = (3 + 1)*numPlanes*sizeof(float);

	dim3 blocks((int)ceil(float(xRes*yRes)/float(blockLength)));
	dim3 threads(blockLength);


	fitFinalPlanesKernel<<<blocks,threads,sharedCount>>>(planeStats, numPlanes, 
		norms, positions, finalSegmentsBuffer, distToPlaneBuffer, xRes, yRes,
		cos(fitAngleThresh), fitDistThresh, iteration);
}

#pragma endregion


__global__ void realignPeaksKernel(PlaneStats* planeStats, Float3SOA normalPeaks, int numNormPeaks, int numDistPeaks, 
								   int xBins, int yBins, int iteration)
{
	int planeIndex = threadIdx.x + threadIdx.y*numDistPeaks + iteration*numNormPeaks*numDistPeaks;
	if(threadIdx.x == 0)//Align to plane with largest peak
	{
		float xI = CUDART_NAN_F;
		float yI = CUDART_NAN_F;
		float count = 0;
		if(planeStats[planeIndex].count > 0)
		{
			//Peaks are in histogram format


			float x = planeStats[planeIndex].norm.x;
			float y = planeStats[planeIndex].norm.y;
			float z = planeStats[planeIndex].norm.z;
			count = planeStats[planeIndex].count;



			if(z < 0.0f)
			{
				x = -x;
				y = -y;
				z = -z;
			}
			//Projected space is well behaved w.r.t indexing when 0 <= z <= 1
			//xI = (x+1.0f)*0.5f*xBins;//x in range of -1 to 1. Map to 0 to 1.0 and multiply by number of bins
			//yI = (y+1.0f)*0.5f*yBins;//x in range of -1 to 1. Map to 0 to 1.0 and multiply by number of bins

			xI = acos(x)*PI_INV_F*xBins;
			yI = acos(y)*PI_INV_F*yBins;
		}

		normalPeaks.x[threadIdx.y] = xI;
		normalPeaks.y[threadIdx.y] = yI;
		normalPeaks.z[threadIdx.y] = count;

	}
}

__host__ void realignPeaks(PlaneStats* planeStats, Float3SOA normalPeaks, int numNormPeaks, int numDistPeaks, 
						   int xBins, int yBins, int iteration)
{
	dim3 threads(numDistPeaks, numNormPeaks);
	dim3 blocks(1);

	realignPeaksKernel<<<blocks,threads>>>(planeStats, normalPeaks, numNormPeaks, numDistPeaks, xBins, yBins, iteration);
}



#define MAX_BLOCK_SIZE 1024
#define MAX_GRID_SIZE 65535

#define NUM_BANKS 32
#define LOG_NUM_BANKS 5 

#define NO_BANK_CONFLICTS


#ifdef NO_BANK_CONFLICTS
#define CONFLICT_FREE_OFFSET(n)    \
	(((n) >> (2 * LOG_NUM_BANKS)))  
#else
#define CONFLICT_FREE_OFFSET(a)    (0)  
#endif


inline int pow2roundup (int x)
{
	if (x < 0)
		return 0;
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x+1;
}


__global__ void generatePlaneCompressionMapKernel(PlaneStats* planeStats, int numPlanes, int* planeIdMap, int* planeIDInvMap, int* planeCountOut)
{
	extern __shared__ int temp[];

	//Now each row is working with it's own row like a normal exclusive scan of an array length width.
	int index = threadIdx.x;
	int offset = 1;
	int n = 2*blockDim.x;//get actual temp padding

	int ai = index;
	int bi = index + n/2;
	int bankOffsetA = CONFLICT_FREE_OFFSET(ai);
	int bankOffsetB = CONFLICT_FREE_OFFSET(bi);

	bool aiFlag = planeStats[ai].count > 0;
	bool biFlag = planeStats[bi].count > 0;
	//Bounds checking, load shared mem
	temp[ai+bankOffsetA] = (ai < numPlanes && aiFlag)?1:0;
	temp[bi+bankOffsetB] = (bi < numPlanes && biFlag)?1:0;

	//Reduction step
	for (int d = n>>1; d > 0; d >>= 1)                  
	{   
		__syncthreads();  //Make sure previous step has completed
		if (index < d)  
		{
			int ai2 = offset*(2*index+1)-1;  
			int bi2 = offset*(2*index+2)-1;  
			ai2 += CONFLICT_FREE_OFFSET(ai2);
			bi2 += CONFLICT_FREE_OFFSET(bi2);

			temp[bi2] += temp[ai2];
		}  
		offset *= 2;  //Adjust offset
	}

	//Reduction complete

	//Clear last element after storing it
	if(index == 0){
		planeCountOut[0] = temp[(n-1)+CONFLICT_FREE_OFFSET(n-1)];
		temp[(n-1)+CONFLICT_FREE_OFFSET(n-1)] = 0;
	}

	//Sweep down
	for (int d = 1; d < n; d *= 2) // traverse down tree & build scan  
	{  
		offset >>= 1;  
		__syncthreads();  //wait for previous step to finish
		if (index < d)                       
		{  
			int ai2 = offset*(2*index+1)-1;  
			int bi2 = offset*(2*index+2)-1;  
			ai2 += CONFLICT_FREE_OFFSET(ai2);
			bi2 += CONFLICT_FREE_OFFSET(bi2);

			//Swap
			float t = temp[ai2];  
			temp[ai2] = temp[bi2];  
			temp[bi2] += t;   
		}  
	}  

	//Sweep complete
	__syncthreads();

	//Writeback (Scatter)
	if(ai < numPlanes )
	{
		if(aiFlag){
			planeIdMap[temp[ai+bankOffsetA]] = ai;
			planeIDInvMap[ai] = temp[ai+bankOffsetA];
		}else
		{
			planeIDInvMap[ai] = -1;
		}
	}
	if(bi < numPlanes)
	{
		if(biFlag){
			planeIdMap[temp[bi+bankOffsetB]] = bi;
			planeIDInvMap[bi] = temp[bi+bankOffsetB];
		}else
		{
			planeIDInvMap[bi] = -1;
		}
	}

}


__host__ void generatePlaneCompressionMap(PlaneStats* planeStats, int numPlanes, int* planeIdMap, int* planeIDInvMap, int* planeCountOut)
{
	dim3 threads(numPlanes>>1);//Two elements per thread
	dim3 blocks(1);
	int sharedSize = (numPlanes+2)*sizeof(int);

	generatePlaneCompressionMapKernel<<<blocks,threads,sharedSize>>>(planeStats, numPlanes, planeIdMap, planeIDInvMap, planeCountOut);
}

__global__ void streamCompactPlaneStatsKernel(PlaneStats* planeStats, int numPlanes, int* planeIdMap, int* planeCount)
{
	int index = threadIdx.x;
	int planeIndex = planeIdMap[index];
	bool isValid = (index < planeCount[0]);

	PlaneStats stats;
	stats.count = 0;
	if(isValid)
	{
		stats = planeStats[planeIndex];
	}

	__syncthreads();

	planeStats[index] = stats;
}

__host__ void compactPlaneStats(PlaneStats* planeStats, int numPlanes, int* planeIdMap,  int* planeCount)
{
	dim3 threads(numPlanes);
	dim3 blocks(1);

	streamCompactPlaneStatsKernel<<<blocks,threads>>>(planeStats, numPlanes, planeIdMap,planeCount);
}


__global__ void computePlaneTangentsKernels(PlaneStats* planeStats,  int* planeCount)
{
	int count = planeCount[0];

	glm::vec3 tangent = glm::vec3(CUDART_NAN_F);
	int index = threadIdx.x;
	if(threadIdx.x < count)
	{

		//T = (A-eye(3)*eig2)*(A(:,1)-[1;0;0]*eig3);

		glm::mat3 S1_n = glm::mat3(glm::vec3(planeStats[index].Sxx, planeStats[index].Sxy, planeStats[index].Sxz), 
			glm::vec3(planeStats[index].Sxy, planeStats[index].Syy, planeStats[index].Syz),
			glm::vec3(planeStats[index].Sxz, planeStats[index].Syz, planeStats[index].Szz));

		glm::mat3 S2 = outerProduct(planeStats[index].centroid.x, planeStats[index].centroid.y, planeStats[index].centroid.z);

		float eig2 = planeStats[index].eigs.y;
		float eig3 = planeStats[index].eigs.z;

		glm::mat3 A = S1_n - S2;
		glm::mat3 Aeig1 = A;
		Aeig1[0][0] -= eig2;
		Aeig1[1][1] -= eig2;
		Aeig1[2][2] -= eig2;
		tangent = Aeig1*(A[0] - glm::vec3(eig3,0.0f,0.0f));

		float length = glm::length(tangent);
		tangent /= length;

		//Compute alignment
		glm::vec3 normal(planeStats[index].norm.x, planeStats[index].norm.y, planeStats[index].norm.z);
		glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));


		if(abs(bitangent.y) > abs(tangent.y))
		{
			//need to swap bitangent and tangent
			tangent = bitangent;
		}

		if(tangent.y < 0)
		{
			tangent = -tangent;
		}

	}
	planeStats[index].tangent = tangent;
}

__host__ void computePlaneTangents(PlaneStats* planeStats, int numPlanes, int* planeCount)
{
	dim3 threads(numPlanes);
	dim3 blocks(1);

	computePlaneTangentsKernels<<<blocks,threads>>>(planeStats, planeCount);

}
