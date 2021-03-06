#include "MeshViewer.h"

//Platform specific code goes here
#include <GL/glew.h>
#include <GL/glut.h>

#pragma region GLUT Hooks
void MeshViewer::glutIdle()
{
	glutPostRedisplay();
}

void MeshViewer::glutDisplay()
{
	MeshViewer::msSelf->display();
}

void MeshViewer::glutKeyboard(unsigned char key, int x, int y)
{
	MeshViewer::msSelf->onKey(key, x, y);
}


void MeshViewer::glutReshape(int w, int h)
{
	MeshViewer::msSelf->reshape(w, h);
}


void MeshViewer::glutMouse(int button, int state, int x, int y)
{
	MeshViewer::msSelf->mouse_click(button, state, x, y);
}

void MeshViewer::glutMotion(int x, int y)
{
	MeshViewer::msSelf->mouse_move(x, y);
}

#pragma endregion

//End platform specific code

#pragma region Variable definitions
const GLuint MeshViewer::quadPositionLocation = 0;
const GLuint MeshViewer::quadTexcoordsLocation = 1;
const char * MeshViewer::quadAttributeLocations[] = { "Position", "Texcoords" };

const GLuint MeshViewer::vbopositionLocation = 0;
const GLuint MeshViewer::vbocolorLocation = 1;
const GLuint MeshViewer::vbonormalLocation = 2;
const char * MeshViewer::vboAttributeLocations[] = { "Position", "Color", "Normal" };


const GLuint MeshViewer::QTMVBOPositionLocation = 0;//vec4
const GLuint MeshViewer::QTMVBOStride = 4;//1*vec4
const GLuint MeshViewer::QTMVBO_PositionOffset = 0;


MeshViewer* MeshViewer::msSelf = NULL;
#pragma endregion


#pragma region Constructors/Destructors
MeshViewer::MeshViewer(RGBDDevice* device, int screenwidth, int screenheight)
{
	//Setup general modules
	msSelf = this;
	mDevice = device;
	mWidth = screenwidth;
	mHeight = screenheight;

	mPauseVisulization = false;

	//Setup default rendering/pipeline settings
	mFilterMode = BILATERAL_FILTER;
	mNormalMode = AVERAGE_GRADIENT_NORMALS;
	mViewState = DISPLAY_MODE_OVERLAY;
	hairyPoints = false;
	mMeshWireframeMode = false;
	mMeshPointMode = false;
	mSpatialSigma = 2.0f;
	mDepthSigma = 0.005f;
	mMaxDepth = 5.0f;

	seconds = time (NULL);
	fpstracker = 0;
	fps = 0.0;
	mLatestTime = 0;
	mLastSubmittedTime = 0;

	resetCamera();
}


MeshViewer::~MeshViewer(void)
{
	msSelf = NULL;
	if(mMeshTracker != NULL)
		delete mMeshTracker;
}

#pragma endregion


//Does not return;
void MeshViewer::run()
{
	glutMainLoop();
}

#pragma region Helper functions
//Framebuffer status helper function
void checkFramebufferStatus(GLenum framebufferStatus) {
	switch (framebufferStatus) {
	case GL_FRAMEBUFFER_COMPLETE_EXT: break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		printf("Attachment Point Unconnected\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		printf("Missing Attachment\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		printf("Dimensions do not match\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		printf("Formats\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		printf("Draw Buffer\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		printf("Read Buffer\n");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		printf("Unsupported Framebuffer Configuration\n");
		break;
	default:
		printf("Unkown Framebuffer Object Failure\n");
		break;
	}
}
#pragma endregion

#pragma region Init/Cleanup Functions

DeviceStatus MeshViewer::init(int argc, char **argv)
{
	//Stream Validation
	if (mDevice->isDepthStreamValid() && mDevice->isColorStreamValid())
	{

		int depthWidth = mDevice->getDepthResolutionX();
		int depthHeight = mDevice->getDepthResolutionY();
		int colorWidth = mDevice->getColorResolutionX();
		int colorHeight = mDevice->getColorResolutionY();

		if (depthWidth == colorWidth &&
			depthHeight == colorHeight)
		{
			mXRes = depthWidth;
			mYRes = depthHeight;

			printf("Color and depth same resolution: D: %dx%d, C: %dx%d\n",
				depthWidth, depthHeight,
				colorWidth, colorHeight);
		}
		else
		{
			printf("Error - expect color and depth to be in same resolution: D: %dx%d, C: %dx%d\n",
				depthWidth, depthHeight,
				colorWidth, colorHeight);
			return DEVICESTATUS_ERROR;
		}
	}
	else if (mDevice->isDepthStreamValid())
	{
		mXRes = mDevice->getDepthResolutionX();
		mYRes = mDevice->getDepthResolutionY();
	}
	else if (mDevice->isColorStreamValid())
	{
		mXRes = mDevice->getColorResolutionX();
		mYRes = mDevice->getColorResolutionY();
	}
	else
	{
		printf("Error - expects at least one of the streams to be valid...\n");
		return DEVICESTATUS_ERROR;
	}

	//Register frame listener
	mDevice->addNewRGBDFrameListener(this);

	//Create mesh tracker and set default values
	mMeshTracker = new MeshTracker(mXRes, mYRes, mDevice->getColorIntrinsics());
	mMeshTracker->setGaussianSpatialSigma(mSpatialSigma);

	//Init rendering cuda code
	initRenderingCuda();

	return initOpenGL(argc, argv);

}

void MeshViewer::initRenderingCuda()
{

}

void MeshViewer::cleanupRenderingCuda()
{

}

DeviceStatus MeshViewer::initOpenGL(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(mWidth, mHeight);
	glutCreateWindow ("CUDA Point Cloud to Mesh");

	//Setup callbacks
	initOpenGLHooks();


	// Init GLEW
	glewInit();
	GLenum err = glewInit();
	if (GLEW_OK != err)
	{
		// Problem: glewInit failed, something is seriously wrong.
		std::cout << "glewInit failed, aborting." << std::endl;
		return DEVICESTATUS_ERROR;
	}

	//Init elements
	initTextures();
	initShader();
	initQuad();
	initPBO();
	initFullScreenPBO();
	initFBO();

	return DEVICESTATUS_OK;
}

void MeshViewer::initOpenGLHooks()
{
	glutKeyboardFunc(glutKeyboard);
	glutDisplayFunc(glutDisplay);
	glutIdleFunc(glutIdle);
	glutReshapeFunc(glutReshape);	
	glutMouseFunc(glutMouse);
	glutMotionFunc(glutMotion);
}

void MeshViewer::initShader()
{

	const char * pass_vert  = "shaders/passVS.glsl";
	const char * color_frag = "shaders/colorFS.glsl";
	const char * abs_frag = "shaders/absFS.glsl";
	const char * depth_frag = "shaders/depthFS.glsl";
	const char * vmap_frag = "shaders/vmapFS.glsl";
	const char * nmap_frag = "shaders/nmapFS.glsl";
	const char * histogram_frag = "shaders/histogramFS.glsl";
	const char * barhistogram_frag = "shaders/barhistogramFS.glsl";
	const char * normalsegments_frag = "shaders/normalsegmentsFS.glsl";
	const char * finalsegments_frag = "shaders/finalsegmentsFS.glsl";
	const char * distsegments_frag = "shaders/distsegmentsFS.glsl";
	const char * projectedsegments_frag = "shaders/projectedsegmentsFS.glsl";
	const char * quadtree_frag = "shaders/quadtreeFS.glsl";

	//Quad Tree Buffer
	const char * qtm_vert = "shaders/qtmVS.glsl";
	const char * qtm_color_frag = "shaders/qtmColorFS.glsl";
	const char * qtm_dist_frag = "shaders/qtmDistFS.glsl";
	const char * green_frag = "shaders/greenFS.glsl";
	const char * blue_frag = "shaders/blueFS.glsl";


	//Color image shader
	color_prog = glslUtility::createProgram(pass_vert, NULL, color_frag, quadAttributeLocations, 2);

	//Absolute value shader
	abs_prog = glslUtility::createProgram(pass_vert, NULL, abs_frag, quadAttributeLocations, 2);

	//DEPTH image shader
	depth_prog = glslUtility::createProgram(pass_vert, NULL, depth_frag, quadAttributeLocations, 2);

	//VMap display debug shader
	vmap_prog = glslUtility::createProgram(pass_vert, NULL, vmap_frag, quadAttributeLocations, 2);

	//NMap display debug shader
	nmap_prog = glslUtility::createProgram(pass_vert, NULL, nmap_frag, quadAttributeLocations, 2);

	histogram_prog = glslUtility::createProgram(pass_vert, NULL, histogram_frag, quadAttributeLocations, 2);

	barhistogram_prog = glslUtility::createProgram(pass_vert, NULL, barhistogram_frag, quadAttributeLocations, 2);

	normalsegments_prog = glslUtility::createProgram(pass_vert, NULL, normalsegments_frag, quadAttributeLocations, 2);

	finalsegments_prog = glslUtility::createProgram(pass_vert, NULL, finalsegments_frag, quadAttributeLocations, 2);

	distsegments_prog = glslUtility::createProgram(pass_vert, NULL, distsegments_frag, quadAttributeLocations, 2);

	projectedsegments_prog = glslUtility::createProgram(pass_vert, NULL, projectedsegments_frag, quadAttributeLocations, 2);

	quadtree_prog= glslUtility::createProgram(pass_vert, NULL, quadtree_frag, quadAttributeLocations, 2);


	//Mesh Programs
	qtm_color_prog  = glslUtility::createProgram(qtm_vert, NULL, qtm_color_frag, quadAttributeLocations, 2);
	qtm_dist_prog  = glslUtility::createProgram(qtm_vert, NULL, qtm_dist_frag, quadAttributeLocations, 2);
	qtm_highlight_blue_prog  = glslUtility::createProgram(qtm_vert, NULL, blue_frag, quadAttributeLocations, 2);
	qtm_highlight_green_prog  = glslUtility::createProgram(qtm_vert, NULL, green_frag, quadAttributeLocations, 2);
}

void MeshViewer::initTextures()
{
	//Clear textures
	if (texture0 != 0 || texture1 != 0 ||  texture2 != 0 || texture3 != 0) {
		cleanupTextures();
	}

	glGenTextures(1, &texture0);
	glGenTextures(1, &texture1);
	glGenTextures(1, &texture2);
	glGenTextures(1, &texture3);

	//Setup Texture 0
	glBindTexture(GL_TEXTURE_2D, texture0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, mXRes, mYRes, 0, GL_RGBA, GL_FLOAT, 0);

	//Setup Texture 1
	glBindTexture(GL_TEXTURE_2D, texture1);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mXRes, mYRes, 0, GL_RGBA, GL_FLOAT,0);

	//Setup Texture 2
	glBindTexture(GL_TEXTURE_2D, texture2);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mXRes, mYRes, 0, GL_RGBA, GL_FLOAT,0);

	//Setup Texture 3
	glBindTexture(GL_TEXTURE_2D, texture3);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mXRes, mYRes, 0, GL_RGBA, GL_FLOAT,0);

	//Setup QTM Texture
	glBindTexture(GL_TEXTURE_2D, qtmTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mMeshTracker->getProjectedTextureBufferWidth(), 
		mMeshTracker->getProjectedTextureBufferWidth(), 0, GL_RGBA, GL_FLOAT,0);

}

void MeshViewer::cleanupTextures()
{
	//Image space textures
	glDeleteTextures(1, &texture0);
	glDeleteTextures(1, &texture1);
	glDeleteTextures(1, &texture2);
	glDeleteTextures(1, &texture3);

}

void MeshViewer::initFBO()
{
	GLenum FBOstatus;
	if(fullscreenFBO != 0)
		cleanupFBO();

	glActiveTexture(GL_TEXTURE9);


	glGenTextures(1, &FBOColorTexture);
	glGenTextures(1, &FBODepthTexture);

	//Set up depth FBO
	glBindTexture(GL_TEXTURE_2D, FBODepthTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, mWidth, mHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);


	//Setup point cloud texture
	glBindTexture(GL_TEXTURE_2D, FBOColorTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mWidth, mHeight, 0, GL_RGBA, GL_FLOAT,0);

	glGenFramebuffers(1, &fullscreenFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, fullscreenFBO);
	glViewport(0,0,(GLsizei)mWidth, (GLsizei)mHeight);


	glReadBuffer(GL_NONE);
	GLint color_loc = glGetFragDataLocation(qtm_color_prog,"FragColor");
	GLenum draws [1];
	draws[color_loc] = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, draws);

	glBindTexture(GL_TEXTURE_2D, FBODepthTexture);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, FBODepthTexture, 0);
	glBindTexture(GL_TEXTURE_2D, FBOColorTexture);    
	glFramebufferTexture(GL_FRAMEBUFFER, draws[color_loc], FBOColorTexture, 0);

	FBOstatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(FBOstatus != GL_FRAMEBUFFER_COMPLETE) {
		printf("GL_FRAMEBUFFER_COMPLETE failed, CANNOT use FBO[0]\n");
		checkFramebufferStatus(FBOstatus);
	}

	// switch back to window-system-provided framebuffer
	glClear(GL_DEPTH_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void MeshViewer::cleanupFBO()
{

	glDeleteTextures(1,&FBODepthTexture);
	glDeleteTextures(1,&FBOColorTexture);
	glDeleteFramebuffers(1,&fullscreenFBO);
}

void MeshViewer::initPBO()
{
	// Generate a buffer ID called a PBO (Pixel Buffer Object)
	if(imagePBO0){
		glDeleteBuffers(1, &imagePBO0);
	}

	if(imagePBO1){
		glDeleteBuffers(1, &imagePBO1);
	}

	if(imagePBO2){
		glDeleteBuffers(1, &imagePBO2);
	}

	int num_texels = mXRes*mYRes;
	int num_values = num_texels * 4;
	int size_tex_data = sizeof(GLfloat) * num_values;
	glGenBuffers(1,&imagePBO0);
	glGenBuffers(1,&imagePBO1);
	glGenBuffers(1,&imagePBO2);

	// Make this the current UNPACK buffer (OpenGL is state-based)
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, imagePBO0);

	// Allocate data for the buffer. 4-channel float image
	glBufferData(GL_PIXEL_UNPACK_BUFFER, size_tex_data, NULL, GL_DYNAMIC_COPY);
	cudaGLRegisterBufferObject( imagePBO0);


	// Make this the current UNPACK buffer (OpenGL is state-based)
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, imagePBO1);

	// Allocate data for the buffer. 4-channel float image
	glBufferData(GL_PIXEL_UNPACK_BUFFER, size_tex_data, NULL, GL_DYNAMIC_COPY);
	cudaGLRegisterBufferObject( imagePBO1);


	// Make this the current UNPACK buffer (OpenGL is state-based)
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, imagePBO2);

	// Allocate data for the buffer. 4-channel float image
	glBufferData(GL_PIXEL_UNPACK_BUFFER, size_tex_data, NULL, GL_DYNAMIC_COPY);
	cudaGLRegisterBufferObject( imagePBO2);
}

void MeshViewer::initFullScreenPBO()
{
	// Generate a buffer ID called a PBO (Pixel Buffer Object)
	if(fullscreenPBO){
		glDeleteBuffers(1, &fullscreenPBO);
	}

	int num_texels = mWidth*mHeight;
	int num_values = num_texels * 4;
	int size_tex_data = sizeof(GLfloat) * num_values;
	glGenBuffers(1,&fullscreenPBO);

	// Make this the current UNPACK buffer (OpenGL is state-based)
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, fullscreenPBO);

	// Allocate data for the buffer. 4-channel float image
	glBufferData(GL_PIXEL_UNPACK_BUFFER, size_tex_data, NULL, GL_DYNAMIC_COPY);
	cudaGLRegisterBufferObject( fullscreenPBO);
}

void MeshViewer::initQuad() {
	vertex2_t verts [] = { {vec3(-1,1,0),vec2(0,0)},
	{vec3(-1,-1,0),vec2(0,1)},
	{vec3(1,-1,0),vec2(1,1)},
	{vec3(1,1,0),vec2(1,0)}};

	unsigned short indices[] = { 0,1,2,0,2,3};

	//Allocate vertex array
	//Vertex arrays encapsulate a set of generic vertex attributes and the buffers they are bound too
	//Different vertex array per mesh.
	glGenVertexArrays(1, &(device_quad.vertex_array));
	glBindVertexArray(device_quad.vertex_array);


	//Allocate vbos for data
	glGenBuffers(1,&(device_quad.vbo_data));
	glGenBuffers(1,&(device_quad.vbo_indices));

	//Upload vertex data
	glBindBuffer(GL_ARRAY_BUFFER, device_quad.vbo_data);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	//Use of strided data, Array of Structures instead of Structures of Arrays
	glVertexAttribPointer(quadPositionLocation, 3, GL_FLOAT, GL_FALSE,sizeof(vertex2_t),0);
	glVertexAttribPointer(quadTexcoordsLocation, 2, GL_FLOAT, GL_FALSE,sizeof(vertex2_t),(void*)sizeof(vec3));
	glEnableVertexAttribArray(quadPositionLocation);
	glEnableVertexAttribArray(quadTexcoordsLocation);

	//indices
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, device_quad.vbo_indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6*sizeof(GLushort), indices, GL_STATIC_DRAW);
	device_quad.num_indices = 6;
	//Unplug Vertex Array
	glBindVertexArray(0);
}

void MeshViewer::initQuadtreeMeshVBO()
{
	glGenBuffers(1,&qtm_VBO);
	glGenBuffers(1,&qtm_triangleIBO);

	cudaGLRegisterBufferObject( qtm_VBO);
	cudaGLRegisterBufferObject( qtm_triangleIBO);

}

#pragma endregion

#pragma region Rendering Helper Functions

void MeshViewer::drawQuadTreeMeshToFrameBuffer(QuadTreeMesh mesh, GLuint prog)
{

	//Setup VBO
	glUseProgram(prog);

	glEnableVertexAttribArray(QTMVBOPositionLocation);

	glBindBuffer(GL_ARRAY_BUFFER, qtm_VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, qtm_triangleIBO);

	//Setup interleaved buffer
	glVertexAttribPointer(QTMVBOPositionLocation, 4, GL_FLOAT, GL_FALSE, QTMVBOStride*sizeof(GLfloat), 
		(void*)(QTMVBO_PositionOffset*sizeof(GLfloat))); 

	//Fill buffer with data
	glBufferData(GL_ARRAY_BUFFER, sizeof(float4)*mesh.numVerts, mesh.vertices.get(), GL_DYNAMIC_DRAW);//Initialize
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2*3*mesh.numVerts*sizeof(GLuint), mesh.triangleIndices.get(), GL_DYNAMIC_DRAW);



	//Setup uniforms
	mat4 persp = glm::perspective(mCamera.fovy, float(mWidth)/float(mHeight), mCamera.zNear, mCamera.zFar);
	mat4 viewmat = glm::lookAt(mCamera.eye, mCamera.eye+mCamera.view, mCamera.up);
	mat4 viewInvTrans = inverse(transpose(viewmat));


	glUniformMatrix4fv(glGetUniformLocation(prog, "u_projMatrix"),1, GL_FALSE, &persp[0][0] );
	glUniformMatrix4fv(glGetUniformLocation(prog, "u_viewMatrix"),1, GL_FALSE, &viewmat[0][0] );
	glUniformMatrix4fv(glGetUniformLocation(prog, "u_viewInvTrans"),1, GL_FALSE, &viewInvTrans[0][0] );
	glUniformMatrix4fv(glGetUniformLocation(prog, "u_modelTransform"),1, GL_FALSE, &mesh.TplaneTocam[0][0] );


	//Bind texture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, qtmTexture);
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F , mesh.mWidth, mesh.mHeight, 0, GL_RGBA, GL_FLOAT, mesh.rgbhTexture.get());

	glUniform1i(glGetUniformLocation(prog, "u_Texture0"),0);



	if(mesh.numVerts > 0){
		glDrawElements(GL_TRIANGLES, mesh.numVerts*2*3, GL_UNSIGNED_INT, NULL);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}


//Normalized device coordinates (-1 : 1, -1 : 1) center of viewport, and scale being 
void MeshViewer::drawQuad(GLuint prog, float xNDC, float yNDC, float widthScale, float heightScale, float textureScale, GLuint* textures, int numTextures)
{
	//Setup program and uniforms
	glUseProgram(prog);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	mat4 persp = mat4(1.0f);//Identity
	mat4 viewmat = mat4(widthScale, 0.0f, 0.0f, 0.0f,
		0.0f, heightScale, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		xNDC, yNDC, 0.0f, 1.0f);


	glUniformMatrix4fv(glGetUniformLocation(prog, "u_projMatrix"),1, GL_FALSE, &persp[0][0] );
	glUniformMatrix4fv(glGetUniformLocation(prog, "u_viewMatrix"),1, GL_FALSE, &viewmat[0][0] );

	//Setup textures
	int location = -1;
	switch(numTextures){
	case 5:
		if ((location = glGetUniformLocation(prog, "u_Texture4")) != -1)
		{
			//has texture
			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, textures[4]);
			glUniform1i(location,4);
		}
	case 4:
		if ((location = glGetUniformLocation(prog, "u_Texture3")) != -1)
		{
			//has texture
			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, textures[3]);
			glUniform1i(location,3);
		}
	case 3:
		if ((location = glGetUniformLocation(prog, "u_Texture2")) != -1)
		{
			//has texture
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, textures[2]);
			glUniform1i(location,2);
		}
	case 2:
		if ((location = glGetUniformLocation(prog, "u_Texture1")) != -1)
		{
			//has texture
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, textures[1]);
			glUniform1i(location,1);
		}
	case 1:
		if ((location = glGetUniformLocation(prog, "u_Texture0")) != -1)
		{
			//has texture
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, textures[0]);
			glUniform1i(location,0);
		}
	}
	if ((location = glGetUniformLocation(prog, "u_TextureScale")) != -1)
	{
		//has texture scale parameter
		glUniform1f(location,textureScale);
	}

	//Draw quad
	glBindVertexArray(device_quad.vertex_array);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, device_quad.vbo_indices);

	glDrawElements(GL_TRIANGLES, device_quad.num_indices, GL_UNSIGNED_SHORT,0);

	glBindVertexArray(0);
}

bool MeshViewer::drawColorImageBufferToTexture(GLuint texture)
{
	float4* dptr;
	cudaGLMapBufferObject((void**)&dptr, imagePBO0);
	drawColorImageBufferToPBO(dptr, mMeshTracker->getColorImageDevicePtr(), mXRes, mYRes);
	cudaGLUnmapBufferObject(imagePBO0);
	//Draw to texture
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);

	return true;
}

bool MeshViewer::drawDepthImageBufferToTexture(GLuint texture)
{	
	float4* dptr;
	cudaGLMapBufferObject((void**)&dptr, imagePBO0);
	drawDepthImageBufferToPBO(dptr,  mMeshTracker->getDepthImageDevicePtr(), mXRes, mYRes);
	cudaGLUnmapBufferObject(imagePBO0);

	//Draw to texture
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);


	return true;
}


void MeshViewer::drawPlaneProjectedTexturetoTexture(GLuint texture, int planeNum)
{
	float4* dptrTexture;
	cudaGLMapBufferObject((void**)&dptrTexture, imagePBO0);

	clearPBO(dptrTexture, mXRes, mYRes, 0.0f);
	ProjectionParameters params = mMeshTracker->getHostProjectionParameters(planeNum);
	if(planeNum < mMeshTracker->getHostNumDetectedPlanes())
	{
		drawPlaneProjectedTexturetoPBO(dptrTexture, 
			mMeshTracker->getProjectedTexture(planeNum),
			params.destWidth, params.destHeight,
			mMeshTracker->getProjectedTextureBufferWidth(),
			mXRes, mYRes);
	}
	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void MeshViewer::drawQuadtreetoTexture(GLuint texture, int planeNum)
{
	float4* dptrTexture;
	cudaGLMapBufferObject((void**)&dptrTexture, imagePBO0);

	clearPBO(dptrTexture, mXRes, mYRes, 0.0f);
	ProjectionParameters params = mMeshTracker->getHostProjectionParameters(planeNum);
	if(planeNum < mMeshTracker->getHostNumDetectedPlanes())
	{
		drawQuadtreetoPBO(dptrTexture, 
			mMeshTracker->getQuadtreeBuffer(planeNum),
			params.destWidth, params.destHeight,
			mMeshTracker->getProjectedTextureBufferWidth(),
			mXRes, mYRes);
	}
	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}
void MeshViewer::drawVMaptoTexture(GLuint texture, int level)
{
	float4* dptrVMap;
	cudaGLMapBufferObject((void**)&dptrVMap, imagePBO0);

	clearPBO(dptrVMap, mXRes, mYRes, 0.0f);
	drawVMaptoPBO(dptrVMap, mMeshTracker->getVMapPyramid(), level, mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

}

void MeshViewer::drawNMaptoTexture(GLuint texture, int level)
{
	float4* dptrNMap;
	cudaGLMapBufferObject((void**)&dptrNMap, imagePBO0);

	clearPBO(dptrNMap, mXRes, mYRes, 0.0f);
	drawNMaptoPBO(dptrNMap, mMeshTracker->getNMapPyramid(), level, mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

}


void MeshViewer::drawNormalHistogramtoTexture(GLuint texture)
{
	float4* dptrNMap;
	cudaGLMapBufferObject((void**)&dptrNMap, imagePBO0);

	clearPBO(dptrNMap, mXRes, mYRes, 0.0f);
	drawNormalVoxelsToPBO(dptrNMap, mMeshTracker->getDeviceNormalHistogram(), mXRes, mYRes, 
		mMeshTracker->getNormalXSubdivisions(), mMeshTracker->getNormalYSubdivisions());

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

}


void MeshViewer::drawDistanceHistogramtoTexture(GLuint texture, vec3 color, int scale, int peak)
{
	float4* pbo;
	cudaGLMapBufferObject((void**)&pbo, imagePBO0);

	clearPBO(pbo, mXRes, mYRes, 0.0f);

	int* d_histPointer = mMeshTracker->getDistanceHistogram(peak);
	if(d_histPointer == NULL)
		return;

	drawScaledHistogramToPBO(pbo, d_histPointer, color, scale, mMeshTracker->getDistanceHistogramSize(), mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

}


void MeshViewer::drawRGBMaptoTexture(GLuint texture, int level)
{
	float4* dptrRGBMap;
	cudaGLMapBufferObject((void**)&dptrRGBMap, imagePBO0);

	clearPBO(dptrRGBMap, mXRes, mYRes, 0.0f);
	drawRGBMaptoPBO(dptrRGBMap, mMeshTracker->getRGBMapSOA(), level, mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void MeshViewer::drawNormalSegmentsToTexture(GLuint texture, int level)
{
	float4* dptrNormalSegmentsMap;
	cudaGLMapBufferObject((void**)&dptrNormalSegmentsMap, imagePBO0);

	clearPBO(dptrNormalSegmentsMap, mXRes, mYRes, 0.0f);
	drawNormalSegmentsToPBO(dptrNormalSegmentsMap, mMeshTracker->getNormalSegments(), mMeshTracker->getPlaneProjectedDistance(),
		mXRes>>level, mYRes>>level, mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}



void MeshViewer::drawFinalSegmentsToTexture(GLuint texture)
{
	float4* dptrNormalSegmentsMap;
	cudaGLMapBufferObject((void**)&dptrNormalSegmentsMap, imagePBO0);

	clearPBO(dptrNormalSegmentsMap, mXRes, mYRes, 0.0f);
	drawSegmentsDataToPBO(dptrNormalSegmentsMap, mMeshTracker->getFinalSegments(), mMeshTracker->getFinalFitDistance(), 
		mMeshTracker->getProjectedSx(),  mMeshTracker->getProjectedSy(),  
		mXRes, mYRes, mXRes, mYRes);

	cudaGLUnmapBufferObject(imagePBO0);

	//Unpack to textures
	glActiveTexture(GL_TEXTURE12);
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, imagePBO0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mXRes, mYRes, 
		GL_RGBA, GL_FLOAT, NULL);

	//Unbind buffers
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
}

void MeshViewer::resetCamera()
{
	mCamera.eye = vec3(0.0f);
	mCamera.view = vec3(0.0f, 0.0f, 1.0f);
	mCamera.up = vec3(0.0f, -1.0f, 0.0f);


	//theta_x/2 = tan_inv( (width/2) / fx ) 
	//theta_y/2 = tan_inv( (height/2) / fy ) 
	Intrinsics intr = mDevice->getDepthIntrinsics();
	float fovy2 = atan2(mDevice->getDepthResolutionY(), intr.fy);
	mCamera.fovy = degrees(2*fovy2);
	mCamera.zFar = 100.0f;
	mCamera.zNear = 0.01;
}

#pragma endregion

#pragma region Event Handlers
void MeshViewer::onNewRGBDFrame(RGBDFramePtr frame)
{
	mLatestFrame = frame;
	if(mLatestFrame != NULL)
	{
		if(mLatestFrame->hasColor())
		{
			mColorArray = mLatestFrame->getColorArray();
		}

		if(mLatestFrame->hasDepth())
		{
			mDepthArray = mLatestFrame->getDepthArray();
			mLatestTime = mLatestFrame->getDepthTimestamp();
		}
	}
}

#pragma endregion

void MeshViewer::display()
{
	//Update frame counter
	time_t seconds2 = time (NULL);

	fpstracker++;
	if(seconds2-seconds >= 1){
		fps = fpstracker/(seconds2-seconds);
		fpstracker = 0;
		seconds = seconds2;
	}



	cudaDeviceSynchronize();
	checkCUDAError("Loop Error Clear");



	//=====Tracker Pipeline=====
	//Check if log playback has restarted (special edge case)
	if(mLastSubmittedTime > mLatestTime){
		//Maybe stream has restarted playback?
		cout << "Reseting tracking, because timestamp" << endl;
		mMeshTracker->resetTracker();
		mLastSubmittedTime = 0;
	}

	//Check if we have a new frame
	if(mLatestTime > mLastSubmittedTime)
	{
		boost::timer::cpu_timer t;

		//Now we have new data, so run pipeline
		mLastSubmittedTime = mLatestTime;

		//Grab local copy of latest frames
		ColorPixelArray localColorArray = mColorArray;
		DPixelArray localDepthArray = mDepthArray;

		//Push buffers
		mMeshTracker->pushRGBDFrameToDevice(localColorArray, localDepthArray, mLatestTime);

		mMeshTracker->deleteQuadTreeMeshes();
		cudaDeviceSynchronize();

		mMeshTracker->buildRGBSOA();

		switch(mFilterMode)
		{
		case BILATERAL_FILTER:
			mMeshTracker->buildVMapBilateralFilter(mMaxDepth, mDepthSigma);
			break;
		case GAUSSIAN_FILTER:
			mMeshTracker->buildVMapGaussianFilter(mMaxDepth);
			break;
		case NO_FILTER:
		default:
			mMeshTracker->buildVMapNoFilter(mMaxDepth);
			break;

		}

		switch(mNormalMode)
		{
		case SIMPLE_NORMALS:
			mMeshTracker->buildNMapSimple();
			break;
		case AVERAGE_GRADIENT_NORMALS:
			mMeshTracker->buildNMapAverageGradient();
			break;
		}


		//Launch kernels for subsampling
		mMeshTracker->subsamplePyramids();

		mMeshTracker->GPUSimpleSegmentation();

		mMeshTracker->ReprojectPlaneTextures();

		cudaDeviceSynchronize();

		int millisec = t.elapsed().wall / 1000000;
		//Update title
		stringstream title;
		title << "RGBD to Mesh Visualization | (" << millisec << " ms)  " << (int)fps  << "FPS";
		glutSetWindowTitle(title.str().c_str());

	}//=====End of pipeline code=====




	//=====RENDERING======
	if(!mPauseVisulization)
	{
		glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		int numMeshes = 0;
		int* elementArray;
		vector<QuadTreeMesh>* meshes  = NULL;
		switch(mViewState)
		{
		case DISPLAY_MODE_DEPTH:
			drawVMaptoTexture(texture0, 0);

			drawQuad(depth_prog, 0, 0, 1, 1, 1.0, &texture0, 1);
			break;
		case DISPLAY_MODE_IMAGE:
			drawRGBMaptoTexture(texture1, 0);

			drawQuad(color_prog, 0, 0, 1, 1, 1.0, &texture1, 1);
			break;
		case DISPLAY_MODE_OVERLAY:
			drawVMaptoTexture(texture0, 0);
			drawRGBMaptoTexture(texture1, 0);


			drawQuad(color_prog, 0, 0, 1, 1, 1.0, &texture1, 1);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);//Alpha blending
			drawQuad(depth_prog, 0, 0, 1, 1, 1.0, &texture0, 1);
			glDisable(GL_BLEND);
			break;
		case DISPLAY_MODE_HISTOGRAM_COMPARE:
			drawNormalHistogramtoTexture(texture0);
			drawNMaptoTexture(texture1, 0);
			drawFinalSegmentsToTexture(texture2);
			drawRGBMaptoTexture(texture3,0);

			drawQuad(histogram_prog, -0.5,  0.5, 0.5, 0.5, 0.1,  &texture0, 1);//UL histogram
			drawQuad(nmap_prog,		 0.5, -0.5, 0.5, 0.5, 1.0, &texture1, 1);//LR
			drawQuad(finalsegments_prog, -0.5, -0.5, 0.5, 0.5, 1.0,  &texture2, 1);//LL
			drawQuad(color_prog,  0.5,  0.5, 0.5, 0.5, 1.0, &texture3, 1);//UR


			break;
		case DISPLAY_MODE_NMAP_DEBUG:
			drawNMaptoTexture(texture0, 0);
			drawNMaptoTexture(texture1, 1);
			drawNMaptoTexture(texture2, 2);
			drawColorImageBufferToTexture(texture3);

			drawQuad(nmap_prog,  0.5,  0.5, 0.5, 0.5, 1.0, &texture0, 1);//UR Level0 NMap
			drawQuad(nmap_prog,  0.5, -0.5, 0.5, 0.5, 0.5,  &texture1, 1);//LR Level1 NMap
			drawQuad(nmap_prog, -0.5, -0.5, 0.5, 0.5, 0.25,  &texture2, 1);//LL Level2 NMap
			drawQuad(color_prog, -0.5,  0.5, 0.5, 0.5, 1.0,  &texture3, 1);//UL Original depth
			break;

		case DISPLAY_MODE_VMAP_DEBUG:
			drawVMaptoTexture(texture0, 0);
			drawVMaptoTexture(texture1, 1);
			drawVMaptoTexture(texture2, 2);
			drawDepthImageBufferToTexture(texture3);

			drawQuad(vmap_prog,  0.5,  0.5, 0.5, 0.5, 1.0, &texture0, 1);//UR Level0 VMap
			drawQuad(vmap_prog,  0.5, -0.5, 0.5, 0.5, 0.5,  &texture1, 1);//LR Level1 VMap
			drawQuad(vmap_prog, -0.5, -0.5, 0.5, 0.5, 0.25,  &texture2, 1);//LL Level2 VMap
			drawQuad(depth_prog, -0.5,  0.5, 0.5, 0.5, 1.0,  &texture3, 1);//UL Original depth
			break;
		case DISPLAY_MODE_SEGMENTATION_DEBUG:
			drawNormalSegmentsToTexture(texture0, 2);
			drawQuad(normalsegments_prog, -0.5, 0.5, 0.5, 0.5, 0.25,  &texture0, 1);//UL
			drawNormalHistogramtoTexture(texture0);
			drawQuad(histogram_prog, -0.5,  -0.5, 0.5, 0.5, 0.1,  &texture0, 1);//LL

			//Draw all peak histograms
			drawDistanceHistogramtoTexture(texture0, vec3(1,0,0), 10000, 0);
			drawQuad(barhistogram_prog, 0.5, 0.875, 0.5, 0.125, 1.0, &texture0, 1);//UR
			drawDistanceHistogramtoTexture(texture0, vec3(0,1,0), 10000, 1);
			drawQuad(barhistogram_prog, 0.5, 0.625, 0.5, 0.125, 1.0, &texture0, 1);//UR
			drawDistanceHistogramtoTexture(texture0, vec3(1,1,0), 10000, 2);
			drawQuad(barhistogram_prog, 0.5, 0.375, 0.5, 0.125, 1.0, &texture0, 1);//UR
			drawDistanceHistogramtoTexture(texture0, vec3(0,0,1), 10000, 3);
			drawQuad(barhistogram_prog, 0.5, 0.125, 0.5, 0.125, 1.0, &texture0, 1);//UR

			//Draw final segmentation
			drawFinalSegmentsToTexture(texture0);
			drawQuad(finalsegments_prog, 0.5, -0.5, 0.5, 0.5, 1.0,  &texture0, 1);//LR

			break;
		case DISPLAY_MODE_PROJECTION_DEBUG:
			//Draw final segmentation
			drawFinalSegmentsToTexture(texture0);
			drawPlaneProjectedTexturetoTexture(texture1, mMeshTracker->getHostNumDetectedPlanes()-1);

			drawQuadtreetoTexture(texture2, mMeshTracker->getHostNumDetectedPlanes()-1);
			drawQuad(quadtree_prog,			-0.5, -0.5, 0.5, 0.5, 1.0,  &texture2, 1);//LL

			//drawQuad(distsegments_prog,  -0.5, -0.5, 0.5, 0.5, 1.0,  &texture0, 1);//LL
			drawQuad(finalsegments_prog,	-0.5, 0.5, 0.5, 0.5, 1.0,  &texture0, 1);//UL
			drawQuad(projectedsegments_prog, 0.5, 0.5, 0.5, 0.5, 1.0,  &texture0, 1);//UR
			drawQuad(color_prog,			 0.5, -0.5, 0.5, 0.5, 1.0, &texture1, 1);//LR
			break;
		case DISPLAY_MODE_QUADTREE:
			meshes = mMeshTracker->getQuadTreeMeshes();
			numMeshes = meshes->size();
			//Bind FBO
			glDisable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D,0); //Bad mojo to unbind the framebuffer using the texture
			glBindFramebuffer(GL_FRAMEBUFFER, fullscreenFBO);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);



			glEnable(GL_CULL_FACE);
			for(int i = 0; i < numMeshes; i++)
			{
				drawQuadTreeMeshToFrameBuffer(meshes->at(i),qtm_color_prog);
				//cout << "{" << meshes->at(i).stats.centroid.x << ',' << 
				//	meshes->at(i).stats.centroid.y << ',' << meshes->at(i).stats.centroid.z << '}' << endl;
			}

			glDisable(GL_CULL_FACE);

				
			if(mMeshWireframeMode){
				glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
				glLineWidth(2.0f);
				for(int i = 0; i < numMeshes; i++)
				{
					drawQuadTreeMeshToFrameBuffer(meshes->at(i),qtm_highlight_green_prog);
					//cout << "{" << meshes->at(i).stats.centroid.x << ',' << 
					//	meshes->at(i).stats.centroid.y << ',' << meshes->at(i).stats.centroid.z << '}' << endl;
				}
				glLineWidth(1.0f);
				glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			}

			
			if(mMeshPointMode){
				glPointSize(5.0f);
				glPolygonMode( GL_FRONT_AND_BACK, GL_POINT );
				for(int i = 0; i < numMeshes; i++)
				{
					drawQuadTreeMeshToFrameBuffer(meshes->at(i),qtm_highlight_blue_prog);
					//cout << "{" << meshes->at(i).stats.centroid.x << ',' << 
					//	meshes->at(i).stats.centroid.y << ',' << meshes->at(i).stats.centroid.z << '}' << endl;
				}
				glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
				glPointSize(1.0f);
			}
			

			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDisable(GL_CULL_FACE);
			drawQuad(color_prog, 0.0, 0.0, 1.0, -1.0, 1.0, &FBOColorTexture, 1);//Fill Screen
			break;
		case DISPLAY_MODE_NONE:
		default:
			break;
		}

		glutSwapBuffers();
	}
}

#pragma region OpenGL Callbacks
////All the important runtime stuff happens here:

void MeshViewer::onKey(unsigned char key, int /*x*/, int /*y*/)
{
	LogDevice* device = NULL;
	float newPlayback = 1.0;
	vec3 right = vec3(0.0f);

	float cameraHighSpeed = 0.1f;
	float cameraLowSpeed = 0.025f;
	float edgeLengthStep = 0.001f;
	float angle;
	switch (key)
	{
	case 27://ESC
		mDevice->destroyColorStream();
		mDevice->destroyDepthStream();

		mDevice->disconnect();
		mDevice->shutdown();

		cleanupRenderingCuda();
		cleanupTextures();
		cudaDeviceReset();
		exit (0);
		break;
	case '1':
		mViewState = DISPLAY_MODE_OVERLAY;
		break;
	case '2':
		mViewState = DISPLAY_MODE_DEPTH;
		break;
	case '3':
		mViewState = DISPLAY_MODE_IMAGE;
		break;
	case '4':
		mViewState = DISPLAY_MODE_HISTOGRAM_COMPARE;
		break;
	case '5':
		mViewState = DISPLAY_MODE_VMAP_DEBUG;
		break;
	case '6':
		mViewState = DISPLAY_MODE_NMAP_DEBUG;
		break;
	case '7':
		mViewState = DISPLAY_MODE_SEGMENTATION_DEBUG;
		break;
	case '8':
		mViewState = DISPLAY_MODE_PROJECTION_DEBUG;
		break;
	case '9':
		mViewState = DISPLAY_MODE_QUADTREE;
		break;
	case '0':
		mViewState = DISPLAY_MODE_NONE;
		break;
	case 'v':
		mMeshWireframeMode = !mMeshWireframeMode;
		cout << "Wireframe mode: " << mMeshWireframeMode << endl;
		break;
	case 'V':
		mMeshPointMode = !mMeshPointMode;
		cout << "Point mode: " << mMeshPointMode << endl;
	case('r'):
		cout << "Reloading Shaders" <<endl;
		initShader();
		break;
	case('p'):
		cout << "Restarting Playback" << endl;
		device = dynamic_cast<LogDevice*>(mDevice);
		if(device != 0) {
			// old was safely casted to LogDevice
			device->restartPlayback();
		}

		break;
	case 'P':
		mPauseVisulization = !mPauseVisulization;

		if(mPauseVisulization)
			cout << "Pause Visualization" << endl;
		else
			cout << "Resume Visualization" << endl;
		break;
	case '=':
		device = dynamic_cast<LogDevice*>(mDevice);
		if(device != 0) {
			// old was safely casted to LogDevice
			newPlayback = device->getPlaybackSpeed()+0.1;
			cout <<"Playback speed: " << newPlayback << endl;
			device->setPlaybackSpeed(newPlayback);		
		}
		break;
	case '-':
		device = dynamic_cast<LogDevice*>(mDevice);
		if(device != 0) {
			// old was safely casted to LogDevice
			newPlayback = device->getPlaybackSpeed()-0.1;
			cout <<"Playback speed: " << newPlayback << endl;
			device->setPlaybackSpeed(newPlayback);		
		}
		break;
	case 'F':
		mCamera.fovy += 0.5;
		cout << "FOVY :" << mCamera.fovy << endl;
		break;
	case 'f':
		mCamera.fovy -= 0.5;
		cout << "FOVY :" << mCamera.fovy << endl;
		break;
	case 'Q':
		mCamera.eye += cameraLowSpeed*mCamera.up;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'Z':
		mCamera.eye -= cameraLowSpeed*mCamera.up;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'W':
		mCamera.eye += cameraLowSpeed*mCamera.view;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'S':
		mCamera.eye -= cameraLowSpeed*mCamera.view;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'D':
		right = normalize(cross(mCamera.view, mCamera.up));
		mCamera.eye += cameraLowSpeed*right;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'A':
		right = normalize(cross(mCamera.view, mCamera.up));
		mCamera.eye -= cameraLowSpeed*right;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;

	case 'q':
		mCamera.eye += cameraHighSpeed*mCamera.up;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'z':
		mCamera.eye -= cameraHighSpeed*mCamera.up;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'w':
		mCamera.eye += cameraHighSpeed*mCamera.view;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 's':
		mCamera.eye -= cameraHighSpeed*mCamera.view;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'd':
		right = normalize(cross(mCamera.view, mCamera.up));
		mCamera.eye += cameraHighSpeed*right;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'a':
		right = normalize(cross(mCamera.view, mCamera.up));
		mCamera.eye -= cameraHighSpeed*right;
		cout << "Camera Eye: " << mCamera.eye.x << " " << mCamera.eye.y << " " << mCamera.eye.z << endl;
		break;
	case 'x':
		resetCamera();
		cout << "Reset Camera" << endl;
		break;
	case 'h':
		hairyPoints = !hairyPoints;
		cout << "Toggle normal hairs" << endl;
		break;
	case 'g':
		mFilterMode = GAUSSIAN_FILTER;
		cout << "Gaussian Filter" << endl;
		break;
	case 'b':
		mFilterMode = BILATERAL_FILTER;
		cout << "Bilateral Filter" << endl;
		break;
	case 'n':
		mFilterMode = NO_FILTER;
		cout << "No Filter" << endl;
		break;
	case '[':
		mSpatialSigma -= 0.1f;
		cout << "Spatial Sigma = " << mSpatialSigma << " Lateral Pixels" << endl;
		mMeshTracker->setGaussianSpatialSigma(mSpatialSigma);
		break;
	case ']':
		mSpatialSigma += 0.1f;
		cout << "Spatial Sigma = " << mSpatialSigma << " Lateral Pixels" << endl;
		mMeshTracker->setGaussianSpatialSigma(mSpatialSigma);
		break;
	case '{':
		mDepthSigma -= 0.005f;
		cout << "Depth Sigma = " << mDepthSigma << " (m)" << endl;
		break;
	case '}':
		mDepthSigma += 0.005f;
		cout << "Depth Sigma = " << mDepthSigma << " (m)" << endl;
		break;
	case '\'':
		mMaxDepth += 0.25f;
		cout << "Max Depth: " << mMaxDepth << " (m)" << endl;
		break;
	case ';':
		mMaxDepth -= 0.25f;
		cout << "Max Depth: " << mMaxDepth << " (m)" << endl;
		break;
	case ':':
		angle = mMeshTracker->get2DSegmentationMaxAngle();
		angle -= 0.5f;
		if(angle > 0.0f)
		{
			cout << "Max Segmentation Angle (degrees): " << angle << endl;
			mMeshTracker->set2DSegmentationMaxAngle(angle);
		}else{
			cout << "Control at limit" << endl;
		}
		break;
	case '"':
		angle = mMeshTracker->get2DSegmentationMaxAngle();
		angle += 0.5f;
		if(angle > 0.0f)
		{
			cout << "Max Segmentation Angle (degrees): " << angle << endl;
			mMeshTracker->set2DSegmentationMaxAngle(angle);
		}else{
			cout << "Control at limit" << endl;
		}
		break;

	case ',':
		mNormalMode = AVERAGE_GRADIENT_NORMALS;
		cout << "Average Gradient Normals Mode"<< endl;
		break;
	case '.':
		mNormalMode = SIMPLE_NORMALS;
		cout << "Simple Normals Mode"<< endl;
		break;
	case 'H':
		{
			ofstream arrayData("segmentationSample.csv"); 

			int* dev_segements = mMeshTracker->getNormalSegments();
			float* dev_segDistance = mMeshTracker->getPlaneProjectedDistance();


			float* dev_rgbX = mMeshTracker->getRGBMapSOA().x[0];
			float* dev_rgbY = mMeshTracker->getRGBMapSOA().y[0];
			float* dev_rgbZ = mMeshTracker->getRGBMapSOA().z[0];


			float* dev_posX = mMeshTracker->getVMapPyramid().x[0];
			float* dev_posY = mMeshTracker->getVMapPyramid().y[0];
			float* dev_posZ = mMeshTracker->getVMapPyramid().z[0];

			float* dev_normX = mMeshTracker->getNMapPyramid().x[0];
			float* dev_normY = mMeshTracker->getNMapPyramid().y[0];
			float* dev_normZ = mMeshTracker->getNMapPyramid().z[0];


			int* segmentIndex = new int[mXRes*mYRes];
			float* segDistance = new float[mXRes*mYRes];


			float* rgbX = new float[mXRes*mYRes];
			float* rgbY = new float[mXRes*mYRes];
			float* rgbZ = new float[mXRes*mYRes];

			float* posX = new float[mXRes*mYRes];
			float* posY = new float[mXRes*mYRes];
			float* posZ = new float[mXRes*mYRes];

			float* normX = new float[mXRes*mYRes];
			float* normY = new float[mXRes*mYRes];
			float* normZ = new float[mXRes*mYRes];

			cudaMemcpy(segmentIndex,  dev_segements, mXRes*mYRes*sizeof(int), cudaMemcpyDeviceToHost);
			cudaMemcpy(segDistance, dev_segDistance, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);

			cudaMemcpy(rgbX, dev_rgbX, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(rgbY, dev_rgbY, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(rgbZ, dev_rgbZ, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(posX, dev_posX, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(posY, dev_posY, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(posZ, dev_posZ, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(normX, dev_normX, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(normY, dev_normY, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);
			cudaMemcpy(normZ, dev_normZ, mXRes*mYRes*sizeof(float), cudaMemcpyDeviceToHost);

			for(int i = 0; i < mXRes*mYRes; ++i) {
				arrayData << posX[i] << ',' << posY[i] << ',' << posZ[i] << ',' 
					<< normX[i] << ',' << normY[i] << ',' << normZ[i] << ',' 
					<< segmentIndex[i] << ','  << segDistance[i] << ','
					<< rgbX[i] << ',' << rgbY[i] << ',' << rgbZ[i] << endl;
			}

			delete segmentIndex;
			delete segDistance;
			delete posX;
			delete posY;
			delete posZ;
			delete normX;
			delete normY;
			delete normZ;

			cout << "Current Segmentation Saved to file" <<endl;
		}
		break;

	}

}

void MeshViewer::reshape(int w, int h)
{
	mWidth = w;
	mHeight = h;


	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glViewport(0,0,(GLsizei)w,(GLsizei)h);



	initTextures();
	initFullScreenPBO();//Refresh fullscreen PBO for new resolution
	initFBO();
}


#pragma region OpenGL Mouse Callbacks

//MOUSE STUFF
void MeshViewer::mouse_click(int button, int state, int x, int y) {
	if(button == GLUT_LEFT_BUTTON) {
		if(state == GLUT_DOWN) {
			dragging = true;
			drag_x_last = x;
			drag_y_last = y;
		}
		else{
			dragging = false;
		}
	}
	if(button == GLUT_RIGHT_BUTTON) {
		if(state == GLUT_DOWN)
		{
			rightclick = true;
		}else{
			rightclick = false;
		}
	}
}

void MeshViewer::mouse_move(int x, int y) {
	if(dragging) {
		float delX = x-drag_x_last;
		float delY = y-drag_y_last;

		//Degrees/pixel
		float rotSpeed = 0.1f;

		vec3 Up = mCamera.up;
		vec3 Right = normalize(cross(mCamera.view, mCamera.up));

		if(rightclick)
		{
			mCamera.view = vec3(glm::rotate(glm::rotate(mat4(1.0f), rotSpeed*delY, Right), rotSpeed*delX, Up)*vec4(mCamera.view, 0.0f));
		}else{
			//Simple rotation
			mCamera.view = vec3(glm::rotate(glm::rotate(mat4(1.0f), rotSpeed*delY, Right), rotSpeed*delX, Up)*vec4(mCamera.view, 0.0f));
		}
		drag_x_last = x;
		drag_y_last = y;
	}
}

#pragma endregion

#pragma endregion