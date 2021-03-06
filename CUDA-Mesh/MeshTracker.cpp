#include "MeshTracker.h"

#pragma region Ctor/Dtor

MeshTracker::MeshTracker(int xResolution, int yResolution, Intrinsics intr)
{
	mXRes = xResolution;
	mYRes = yResolution;
	mIntr = intr;

	//Setup default configuration
	m2DSegmentationMaxAngleFromPeak = 5.0f;


	mPlaneMergeAngleThresh = 5.0f;
	mPlaneMergeDistThresh = 0.025f;

	mPlaneFinalAngleThresh = 15.0f;
	mPlaneFinalDistThresh = 0.015;

	mDistPeakThresholdTight = 0.025;
	mMinDistPeakCount = 800;
	mMinNormalPeakCout = 800;

	mMaxPlanesOutput = MAX_PLANES_TOTAL;

	initBuffers(mXRes, mYRes);

	resetTracker();
}


MeshTracker::~MeshTracker(void)
{
	cleanupBuffers();
}
#pragma endregion

#pragma region Setup/Teardown functions
void MeshTracker::initBuffers(int xRes, int yRes)
{
	int pixCount = xRes*yRes;
	cudaMalloc((void**) &dev_colorImageBuffer,				sizeof(ColorPixel)*pixCount);
	cudaMalloc((void**) &dev_depthImageBuffer,				sizeof(DPixel)*pixCount);


	//Setup SOA buffers, ensuring contiguous memory for pyramids 
	//RGB Pyramid SOA
	createFloat3SOAPyramid(dev_rgbSOA, xRes, yRes);

	//Vertex Map Pyramid SOA. 
	createFloat3SOAPyramid(dev_vmapSOA, xRes, yRes);

	//Normal Map Pyramid SOA. 
	createFloat3SOAPyramid(dev_nmapSOA, xRes, yRes);

	//2D Normal Histogram
	cudaMalloc((void**) &dev_normalVoxels,	NUM_NORMAL_X_SUBDIVISIONS*NUM_NORMAL_Y_SUBDIVISIONS*sizeof(int));

	//Normal Segmentation Results
	cudaMalloc((void**) &dev_normalSegments, xRes*yRes*sizeof(int));
	cudaMalloc((void**) &dev_planeProjectedDistanceMap, xRes*yRes*sizeof(float));

	createFloat3SOA(dev_normalPeaks, MAX_2D_PEAKS_PER_ROUND);

	//Projected distance histograms
	cudaMalloc((void**) &(dev_distanceHistograms[0]), MAX_2D_PEAKS_PER_ROUND*DISTANCE_HIST_COUNT*sizeof(int));
	for(int i = 1; i < MAX_2D_PEAKS_PER_ROUND; i++)//Update other pointers
		dev_distanceHistograms[i] = dev_distanceHistograms[i-1] + DISTANCE_HIST_COUNT;

	cudaMalloc((void**) &(dev_distPeaks[0]), MAX_2D_PEAKS_PER_ROUND*DISTANCE_HIST_MAX_PEAKS*sizeof(float));
	for(int i = 1; i < MAX_2D_PEAKS_PER_ROUND; i++)//Update other pointers
		dev_distPeaks[i] = dev_distPeaks[i-1] + DISTANCE_HIST_COUNT;



	//Plane stats buffers
	cudaMalloc((void**) &dev_planeStats, MAX_PLANES_TOTAL*sizeof(PlaneStats));
	host_planeStats = new PlaneStats[MAX_PLANES_TOTAL];

	cudaMalloc((void**) &dev_finalSegmentsBuffer, xRes*yRes*sizeof(int));
	cudaMalloc((void**) &dev_finalDistanceToPlaneBuffer, xRes*yRes*sizeof(float));

	cudaMalloc((void**) &dev_planeIdMap,  MAX_PLANES_TOTAL*sizeof(int));
	cudaMalloc((void**) &dev_planeInvIdMap,  MAX_PLANES_TOTAL*sizeof(int));
	cudaMalloc((void**) &dev_detectedPlaneCount,  sizeof(int));

	int numBlocks = ceil(xRes/float(AABB_COMPUTE_BLOCKWIDTH))*ceil(yRes/float(AABB_COMPUTE_BLOCKHEIGHT));
	cudaMalloc((void**) &dev_aabbIntermediateBuffer, 
		numBlocks*MAX_PLANES_TOTAL*sizeof(glm::vec4));


	cudaMalloc((void**) &dev_segmentProjectedSx, xRes*yRes*sizeof(float));
	cudaMalloc((void**) &dev_segmentProjectedSy, xRes*yRes*sizeof(float));

	createFloat4SOA(dev_PlaneTexture, MAX_TEXTURE_BUFFER_SIZE*MAX_TEXTURE_BUFFER_SIZE);
	cudaMalloc((void**) &dev_finalTextureBuffer, MAX_TEXTURE_BUFFER_SIZE*MAX_TEXTURE_BUFFER_SIZE*sizeof(float4));

	cudaMalloc((void**) &dev_quadTreeAssembly, MAX_TEXTURE_BUFFER_SIZE*MAX_TEXTURE_BUFFER_SIZE*sizeof(int));
	cudaMalloc((void**) &dev_quadTreeScanResults, MAX_TEXTURE_BUFFER_SIZE*MAX_TEXTURE_BUFFER_SIZE*sizeof(int));
	cudaMalloc((void**) &dev_quadTreeBlockResults, MAX_TEXTURE_BUFFER_SIZE*sizeof(int));


	cudaMalloc((void**) &dev_quadTreeIndexBuffer, QUADTREE_BUFFER_SIZE*6*sizeof(int));//Triangles
	cudaMalloc((void**) &dev_quadTreeVertexBuffer, QUADTREE_BUFFER_SIZE*sizeof(float4));//verticies
	cudaMalloc((void**) &dev_compactCount, sizeof(int));//Number of triangles


	for(int i = 0; i < NUM_FLOAT1_PYRAMID_BUFFERS; ++i)
	{
		createFloat1SOAPyramid(dev_float1PyramidBuffers[i], xRes, yRes);
	}

	for(int i = 0; i < NUM_FLOAT3_PYRAMID_BUFFERS; ++i)
	{
		createFloat3SOAPyramid(dev_float3PyramidBuffers[i], xRes, yRes);
	}


	for(int i = 0; i < NUM_FLOAT1_IMAGE_SIZE_BUFFERS; ++i)
	{
		cudaMalloc((void**) &dev_floatImageBuffers[i], xRes*yRes*sizeof(float));
	}

	//Initialize gaussian spatial kernel
	setGaussianSpatialSigma(1.0f);
}

void MeshTracker::cleanupBuffers()
{
	cudaFree(dev_colorImageBuffer);
	cudaFree(dev_depthImageBuffer);
	freeFloat3SOAPyramid(dev_rgbSOA);
	freeFloat3SOAPyramid(dev_vmapSOA);
	freeFloat3SOAPyramid(dev_nmapSOA);

	cudaFree(dev_normalVoxels);

	cudaFree(dev_normalSegments);
	cudaFree(dev_planeProjectedDistanceMap);

	freeFloat3SOA(dev_normalPeaks);

	cudaFree(dev_distanceHistograms[0]);
	cudaFree(dev_distPeaks[0]);


	cudaFree(dev_planeStats);

	cudaFree(dev_finalSegmentsBuffer);
	cudaFree(dev_finalDistanceToPlaneBuffer);

	cudaFree(dev_planeIdMap);
	cudaFree(dev_planeInvIdMap);
	cudaFree(dev_detectedPlaneCount);
	cudaFree(dev_aabbIntermediateBuffer);

	cudaFree(dev_segmentProjectedSx);
	cudaFree(dev_segmentProjectedSy);

	freeFloat4SOA(dev_PlaneTexture);
	cudaFree(dev_finalTextureBuffer);
	cudaFree(dev_quadTreeAssembly);
	cudaFree(dev_quadTreeScanResults);
	cudaFree(dev_quadTreeBlockResults);

	cudaFree(dev_quadTreeIndexBuffer);
	cudaFree(dev_quadTreeVertexBuffer);
	cudaFree(dev_compactCount);

	for(int i = 0; i < NUM_FLOAT1_PYRAMID_BUFFERS; ++i)
	{
		freeFloat1SOAPyramid(dev_float1PyramidBuffers[i]);
	}

	for(int i = 0; i < NUM_FLOAT3_PYRAMID_BUFFERS; ++i)
	{
		freeFloat3SOAPyramid(dev_float3PyramidBuffers[i]);
	}

	for(int i = 0; i < NUM_FLOAT1_IMAGE_SIZE_BUFFERS; ++i)
	{
		cudaFree(dev_floatImageBuffers[i]);
	}


}


void MeshTracker::createFloat1SOAPyramid(Float1SOAPyramid& dev_pyramid, int xRes, int yRes)
{
	int pixCount = xRes*yRes;
	int pyramidCount = 0;

	for(int i = 0; i < NUM_PYRAMID_LEVELS; ++i)
	{
		pyramidCount += (pixCount >> (i*2));
	}

	cudaMalloc((void**) &dev_pyramid.x[0], sizeof(float)*(pyramidCount));
	//Get convenience pointer offsets
	for(int i = 0; i < NUM_PYRAMID_LEVELS-1; ++i)
	{
		dev_pyramid.x[i+1] = dev_pyramid.x[i] + (pixCount >> (i*2));
	}


}

void MeshTracker::freeFloat1SOAPyramid(Float1SOAPyramid dev_pyramid)
{
	cudaFree(dev_pyramid.x[0]);
}

void MeshTracker::createInt3SOA(Int3SOA& dev_soa, int length)
{
	cudaMalloc((void**) &dev_soa.x, sizeof(int)*3*length);
	dev_soa.y = dev_soa.x + length;
	dev_soa.z = dev_soa.y + length;
}

void MeshTracker::freeInt3SOA(Int3SOA dev_soa)
{
	cudaFree(dev_soa.x);
}


void MeshTracker::createFloat3SOA(Float3SOA& dev_soa, int length)
{
	cudaMalloc((void**) &dev_soa.x, sizeof(float)*3*length);
	dev_soa.y = dev_soa.x + length;
	dev_soa.z = dev_soa.y + length;
}

void MeshTracker::freeFloat3SOA(Float3SOA dev_soa)
{
	cudaFree(dev_soa.x);
}



void MeshTracker::createFloat4SOA(Float4SOA& dev_soa, int length)
{
	cudaMalloc((void**) &dev_soa.x, sizeof(float)*4*length);
	dev_soa.y = dev_soa.x + length;
	dev_soa.z = dev_soa.y + length;
	dev_soa.w = dev_soa.z + length;
}

void MeshTracker::freeFloat4SOA(Float4SOA dev_soa)
{
	cudaFree(dev_soa.x);
}

void MeshTracker::createFloat3SOAPyramid(Float3SOAPyramid& dev_pyramid, int xRes, int yRes)
{
	int pixCount = xRes*yRes;
	int pyramidCount = 0;

	for(int i = 0; i < NUM_PYRAMID_LEVELS; ++i)
	{
		pyramidCount += (pixCount >> (i*2));
	}

	cudaMalloc((void**) &dev_pyramid.x[0], sizeof(float)*3*(pyramidCount));
	//Get convenience pointer offsets
	for(int i = 0; i < NUM_PYRAMID_LEVELS-1; ++i)
	{
		dev_pyramid.x[i+1] = dev_pyramid.x[i] + (pixCount >> (i*2));
	}

	dev_pyramid.y[0] = dev_pyramid.x[0] + pyramidCount;
	for(int i = 0; i < NUM_PYRAMID_LEVELS-1; ++i)
	{
		dev_pyramid.y[i+1] = dev_pyramid.y[i] + (pixCount >> (i*2));
	}


	dev_pyramid.z[0] = dev_pyramid.y[0] + pyramidCount;
	for(int i = 0; i < NUM_PYRAMID_LEVELS-1; ++i)
	{
		dev_pyramid.z[i+1] = dev_pyramid.z[i] + (pixCount >> (i*2));
	}

}

void MeshTracker::freeFloat3SOAPyramid(Float3SOAPyramid dev_pyramid)
{
	cudaFree(dev_pyramid.x[0]);
}




#pragma endregion

#pragma region Pipeline control API
void MeshTracker::resetTracker()
{
	lastFrameTime = 0LL;
	currentFrameTime = 0LL;
	//TODO: Initalize and clear world tree


}

#pragma region Preprocessing
void MeshTracker::pushRGBDFrameToDevice(ColorPixelArray colorArray, DPixelArray depthArray, timestamp time)
{
	lastFrameTime = currentFrameTime;
	currentFrameTime = time;

	cudaMemcpy((void*)dev_depthImageBuffer, depthArray.get(), sizeof(DPixel)*mXRes*mYRes, cudaMemcpyHostToDevice);
	cudaMemcpy((void*)dev_colorImageBuffer, colorArray.get(), sizeof(ColorPixel)*mXRes*mYRes, cudaMemcpyHostToDevice);

}



void MeshTracker::buildRGBSOA()
{
	rgbAOSToSOACUDA(dev_colorImageBuffer, dev_rgbSOA, mXRes, mYRes);
}

void MeshTracker::buildVMapNoFilter(float maxDepth)
{
	buildVMapNoFilterCUDA(dev_depthImageBuffer, dev_vmapSOA, mXRes, mYRes, mIntr, maxDepth);

}

void MeshTracker::buildVMapGaussianFilter(float maxDepth)
{
	flipDepthImageXAxis(dev_depthImageBuffer, mXRes, mYRes);
	buildVMapGaussianFilterCUDA(dev_depthImageBuffer, dev_vmapSOA, mXRes, mYRes, mIntr, maxDepth);

}

void MeshTracker::buildVMapBilateralFilter(float maxDepth, float sigma_t)
{

	flipDepthImageXAxis(dev_depthImageBuffer, mXRes, mYRes);
	buildVMapBilateralFilterCUDA(dev_depthImageBuffer, dev_vmapSOA, mXRes, mYRes, 
		mIntr, maxDepth, sigma_t);


}


void MeshTracker::setGaussianSpatialSigma(float sigma)
{
	setGaussianSpatialKernel(sigma);
}


void MeshTracker::buildNMapSimple()
{

	simpleNormals(dev_vmapSOA, dev_nmapSOA, NUM_PYRAMID_LEVELS, mXRes, mYRes);

}


void MeshTracker::buildNMapAverageGradient()
{
	//Assemble gradient images.

	//For each first level of pyramid
	horizontalGradient(dev_vmapSOA.x[0], dev_float3PyramidBuffers[0].x[0], mXRes, mYRes);
	verticalGradient(  dev_vmapSOA.x[0], dev_float3PyramidBuffers[1].x[0], mXRes, mYRes);

	horizontalGradient(dev_vmapSOA.y[0], dev_float3PyramidBuffers[0].y[0], mXRes, mYRes);
	verticalGradient(  dev_vmapSOA.y[0], dev_float3PyramidBuffers[1].y[0], mXRes, mYRes);

	horizontalGradient(dev_vmapSOA.z[0], dev_float3PyramidBuffers[0].z[0], mXRes, mYRes);
	verticalGradient(  dev_vmapSOA.z[0], dev_float3PyramidBuffers[1].z[0], mXRes, mYRes);

	//Apply filters to float3
	setSeperableKernelGaussian(10.0);

	seperableFilter(dev_float3PyramidBuffers[0].x[0], dev_float3PyramidBuffers[0].y[0], dev_float3PyramidBuffers[0].z[0],
		dev_float3PyramidBuffers[0].x[0], dev_float3PyramidBuffers[0].y[0], dev_float3PyramidBuffers[0].z[0],
		mXRes, mYRes);

	seperableFilter(dev_float3PyramidBuffers[1].x[0], dev_float3PyramidBuffers[1].y[0], dev_float3PyramidBuffers[1].z[0],
		dev_float3PyramidBuffers[1].x[0], dev_float3PyramidBuffers[1].y[0], dev_float3PyramidBuffers[1].z[0],
		mXRes, mYRes);

	computeAverageGradientNormals(dev_float3PyramidBuffers[0], dev_float3PyramidBuffers[1], dev_vmapSOA, dev_nmapSOA, mXRes, mYRes);

}

void MeshTracker::segmentationInnerLoop(int resolutionLevel, int iteration)
{
	float countScale = 1.0f/(1 << (resolutionLevel*2));
	Float3SOA normals;
	normals.x = dev_nmapSOA.x[resolutionLevel];
	normals.y = dev_nmapSOA.y[resolutionLevel];
	normals.z = dev_nmapSOA.z[resolutionLevel];

	Float3SOA positions;
	positions.x = dev_vmapSOA.x[resolutionLevel];
	positions.y = dev_vmapSOA.y[resolutionLevel];
	positions.z = dev_vmapSOA.z[resolutionLevel];


	//Tight tolerance angle segmentation
	segmentNormals2D(normals, positions, dev_normalSegments, dev_planeProjectedDistanceMap, mXRes>>resolutionLevel, mYRes>>resolutionLevel, 
		dev_normalVoxels, NUM_NORMAL_X_SUBDIVISIONS, NUM_NORMAL_Y_SUBDIVISIONS, 
		dev_normalPeaks, MAX_2D_PEAKS_PER_ROUND, m2DSegmentationMaxAngleFromPeak*PI_F/180.0f);

	//Distance histogram generation
	clearHistogram(dev_distanceHistograms[0], DISTANCE_HIST_COUNT, MAX_2D_PEAKS_PER_ROUND);
	generateDistanceHistograms(dev_normalSegments, dev_planeProjectedDistanceMap, 
		mXRes>>resolutionLevel, mYRes>>resolutionLevel, dev_distanceHistograms,
		MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_COUNT, DISTANCE_HIST_MIN, DISTANCE_HIST_MAX);

	distanceHistogramPrimaryPeakDetection(dev_distanceHistograms[0], DISTANCE_HIST_COUNT, MAX_2D_PEAKS_PER_ROUND, dev_distPeaks[0], 
		DISTANCE_HIST_MAX_PEAKS, int(2.0f*mDistPeakThresholdTight/DISTANCE_HIST_RESOLUTION), 
		mMinDistPeakCount*countScale, DISTANCE_HIST_MIN, DISTANCE_HIST_MAX);

	//Segment by distance and assemble plane stats for segments
	clearPlaneStats(dev_planeStats, MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_MAX_PEAKS, MAX_SEGMENTATION_ROUNDS, iteration);

	fineDistanceSegmentation(dev_distPeaks[0], MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_MAX_PEAKS, 
		positions, dev_planeStats, dev_normalSegments, dev_planeProjectedDistanceMap, 
		mXRes>>resolutionLevel, mYRes>>resolutionLevel, mDistPeakThresholdTight, iteration);


	//Process stats and calculate merges
	finalizePlanes(dev_planeStats, MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_MAX_PEAKS, 
		mPlaneMergeAngleThresh*PI_F/180.0f, mPlaneMergeDistThresh, iteration);

}


void MeshTracker::normalHistogramGeneration(int normalHistLevel, int iteration)
{
	clearHistogram(dev_normalVoxels, NUM_NORMAL_X_SUBDIVISIONS, NUM_NORMAL_Y_SUBDIVISIONS);

	computeNormalHistogram(dev_nmapSOA.x[normalHistLevel], dev_nmapSOA.y[normalHistLevel], dev_nmapSOA.z[normalHistLevel], 
		dev_finalSegmentsBuffer,
		dev_normalVoxels, mXRes>>normalHistLevel, mYRes>>normalHistLevel, 
		NUM_NORMAL_X_SUBDIVISIONS, NUM_NORMAL_Y_SUBDIVISIONS, (iteration>0));

	//Detect peaks
	normalHistogramPrimaryPeakDetection(dev_normalVoxels, NUM_NORMAL_X_SUBDIVISIONS, NUM_NORMAL_Y_SUBDIVISIONS, 
		dev_normalPeaks, MAX_2D_PEAKS_PER_ROUND,  PEAK_2D_EXCLUSION_RADIUS, mMinNormalPeakCout/float(1 << normalHistLevel*2),
		(iteration>0)?PEAK_2D_EXCLUSION_RADIUS/2:0);
}

void MeshTracker::GPUSimpleSegmentation()
{
	//Clear buffers
	clearPlaneStats(dev_planeStats, MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_MAX_PEAKS, MAX_SEGMENTATION_ROUNDS, -1);

	//Future LOOP Start
	for(int iter = 0; iter < MAX_SEGMENTATION_ROUNDS; ++iter)
	{
		//Generate normal histogram
		normalHistogramGeneration(0, iter);//Won't work for iterations higher than 0 at resolution levels higher than 0

		segmentationInnerLoop(2, iter);

		//Use plane stats from first pass to better align peaks, then re-segment
		realignPeaks(dev_planeStats, dev_normalPeaks, MAX_2D_PEAKS_PER_ROUND, DISTANCE_HIST_MAX_PEAKS, 
			NUM_NORMAL_X_SUBDIVISIONS, NUM_NORMAL_Y_SUBDIVISIONS, iter);

		segmentationInnerLoop(2, iter);


		Float3SOA normals;
		normals.x = dev_nmapSOA.x[0];
		normals.y = dev_nmapSOA.y[0];
		normals.z = dev_nmapSOA.z[0];

		Float3SOA positions;
		positions.x = dev_vmapSOA.x[0];
		positions.y = dev_vmapSOA.y[0];
		positions.z = dev_vmapSOA.z[0];

		int numPlanes = MAX_2D_PEAKS_PER_ROUND*DISTANCE_HIST_MAX_PEAKS*(iter+1);

		mergePlanes(dev_planeStats, numPlanes,mPlaneMergeAngleThresh*PI_F/180.0f, mPlaneMergeDistThresh);

		fitFinalPlanes(dev_planeStats, numPlanes, 
			normals, positions,  dev_finalSegmentsBuffer, dev_finalDistanceToPlaneBuffer, mXRes, mYRes,
			mPlaneFinalAngleThresh*PI_F/180.0f, mPlaneFinalDistThresh, 0);

	}

	generatePlaneCompressionMap(dev_planeStats, MAX_PLANES_TOTAL, 
		dev_planeIdMap, dev_planeInvIdMap, dev_detectedPlaneCount);

	compactPlaneStats(dev_planeStats,  MAX_PLANES_TOTAL, 
		dev_planeIdMap, dev_detectedPlaneCount);

	computePlaneTangents(dev_planeStats, MAX_PLANES_TOTAL, dev_detectedPlaneCount);
}

int roundnextpow2up (int x)
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

void MeshTracker::ReprojectPlaneTextures()
{

	Float3SOA positions;
	positions.x = dev_vmapSOA.x[0];
	positions.y = dev_vmapSOA.y[0];
	positions.z = dev_vmapSOA.z[0];

	//Compute bounding boxes and do some other work in the meantime like remapping segments to correct ids and generating plane projected 
	computeAABBs(dev_planeStats, dev_planeInvIdMap, dev_aabbIntermediateBuffer, dev_detectedPlaneCount,  
		MAX_PLANES_TOTAL, 
		positions, dev_segmentProjectedSx, dev_segmentProjectedSy, dev_finalSegmentsBuffer, mXRes, mYRes);

	calculateProjectionData(mIntr, dev_planeStats, dev_detectedPlaneCount, 
		MAX_TEXTURE_BUFFER_SIZE, MAX_PLANES_TOTAL, mXRes, mYRes);

	cudaMemcpy(host_planeStats, dev_planeStats, 
		MAX_PLANES_TOTAL*sizeof(PlaneStats), 
		cudaMemcpyDeviceToHost);

	//Plane projection parameters now on host side. Use to dispatch kernels more efficiently
	RGBMapSOA rgbMap;
	rgbMap.r = dev_rgbSOA.x[0];
	rgbMap.g = dev_rgbSOA.y[0];
	rgbMap.b = dev_rgbSOA.z[0];

	host_detectedPlaneCount = 0;
	//For each detected plane
	for(int i = 0; i < mMaxPlanesOutput; ++i){
		if(host_planeStats[i].projParams.destWidth > 0)
		{
			host_detectedPlaneCount++;
			//Offset projections to correct index
			projectTexture(i, (host_planeStats + i), (dev_planeStats + i), 
				dev_PlaneTexture, MAX_TEXTURE_BUFFER_SIZE, 
				rgbMap, dev_finalSegmentsBuffer, dev_finalDistanceToPlaneBuffer,
				mXRes, mYRes);

			//Quadtree decimation
			quadtreeDecimation((host_planeStats + i)->projParams.destWidth, (host_planeStats + i)->projParams.destHeight,
				dev_PlaneTexture, dev_quadTreeAssembly, MAX_TEXTURE_BUFFER_SIZE);

			//Quadtree compression, mesh generation
			int finalTextureWidth = roundnextpow2up((host_planeStats + i)->projParams.destWidth);
			int finalTextureHeight = roundnextpow2up((host_planeStats + i)->projParams.destHeight);

			quadtreeMeshGeneration((host_planeStats + i)->projParams.aabbMeters, 
				(host_planeStats + i)->projParams.destWidth, (host_planeStats + i)->projParams.destHeight,
				dev_quadTreeAssembly, dev_quadTreeScanResults, MAX_TEXTURE_BUFFER_SIZE, 
				dev_quadTreeBlockResults, MAX_TEXTURE_BUFFER_SIZE,
				dev_quadTreeIndexBuffer, dev_quadTreeVertexBuffer, dev_compactCount, &host_quadtreeVertexCount, QUADTREE_BUFFER_SIZE,
				finalTextureWidth, finalTextureHeight, dev_PlaneTexture, dev_finalTextureBuffer);

			//TODO: Collect data on decimation

			//Load mesh back
			glm::mat4 Ttrans = glm::translate(glm::mat4(1.f), host_planeStats[i].centroid);
			//Build rotation matrix from basis vectors in camera space
			glm::vec3 bitan = glm::normalize(glm::cross(host_planeStats[i].norm, host_planeStats[i].tangent));
			glm::mat4 Trot = glm::mat4(glm::vec4(bitan, 0.0f),
				glm::vec4(host_planeStats[i].tangent, 0.0f),
				glm::vec4(host_planeStats[i].norm, 0.0f),
				glm::vec4(0.0f,0.0f,0.0f, 1.0f));


			QuadTreeMesh resultMesh(finalTextureWidth, finalTextureHeight, host_quadtreeVertexCount, host_planeStats[i], Ttrans*Trot);
			//Pull data
			cudaMemcpy(resultMesh.rgbhTexture.get(), dev_finalTextureBuffer, 
				finalTextureWidth*finalTextureHeight*sizeof(float4), cudaMemcpyDeviceToHost);
			
			cudaMemcpy(resultMesh.vertices.get(), dev_quadTreeVertexBuffer, 
				host_quadtreeVertexCount*sizeof(float4), cudaMemcpyDeviceToHost);
			
			cudaMemcpy(resultMesh.triangleIndices.get(), dev_quadTreeIndexBuffer, 
				host_quadtreeVertexCount*6*sizeof(int), cudaMemcpyDeviceToHost);

			host_quadtrees.push_back(resultMesh);
		}else
		{
			//Reached last plane, done
			break;
		}
	}

}


void MeshTracker::deleteQuadTreeMeshes()
{
	host_quadtrees.clear();
}

void MeshTracker::subsamplePyramids()
{
	subsamplePyramidCUDA(dev_vmapSOA, mXRes, mYRes, NUM_PYRAMID_LEVELS);
	subsamplePyramidCUDA(dev_nmapSOA, mXRes, mYRes, NUM_PYRAMID_LEVELS);
	subsamplePyramidCUDA(dev_rgbSOA,  mXRes, mYRes, NUM_PYRAMID_LEVELS);
}


#pragma endregion

#pragma endregion