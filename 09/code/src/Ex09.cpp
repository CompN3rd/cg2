#include <GL/glew.h>
#include <GL/freeglut.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/string_cast.hpp> 

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <stack>
#include <cmath>

#include "ObjLoader.h"
#include "CameraController.h"

#include <sstream>
#include <opencv/cv.h>
#include <opencv/highgui.h>

std::stack<glm::mat4> glm_ProjectionMatrix; 
std::stack<glm::mat4> glm_ModelViewMatrix; 

// OpenGL and GLSL stuff //
void initGL();
void initShader();
void initFBO();

bool enableShader(int pass = 0);
void disableShader();
void deleteShader();
char* loadShaderSource(const char* fileName);
GLuint loadShaderFile(const char* fileName, GLenum shaderType);
// the used shader program //
GLuint shaderProgram = 0;
GLuint shaderPass[2] = {0, 0};
// this map stores uniform locations of our shader program //
std::map<std::string, GLint> uniformLocations;

// this struct helps to keep light source parameter uniforms together //
struct UniformLocation_Light {
	GLint ambient_color;
	GLint diffuse_color;
	GLint specular_color;
	GLint power;
	GLint position;
};
// this map stores the light source uniform locations as 'UniformLocation_Light' structs //
std::map<std::string, UniformLocation_Light> uniformLocations_Lights;

// these structs are also used in the shader code  //
// this helps to access the parameters more easily //
struct Material {
	glm::vec3 ambient_color;
	glm::vec3 diffuse_color;
	glm::vec3 specular_color;
	float specular_shininess;
};

struct LightSource {
	LightSource() : enabled(true) {};
	bool enabled;
	glm::vec3 ambient_color;
	glm::vec3 diffuse_color;
	glm::vec3 specular_color;
	glm::vec3 position;
	float power;
};

// the program uses a list of materials and light sources, which can be chosen during rendering //
unsigned int materialIndex;
unsigned int materialCount;
std::vector<Material> materials;
unsigned int lightCount;
std::vector<LightSource> lights;

// #INFO# Container for texture data //
struct Texture {
	Texture() : isInitialized(false), data(NULL), width(0), height(0), glTextureLocation(0), uniformLocation(-1), uniformEnabledLocation(-1) {};
	bool isInitialized;
	// local data storage
	unsigned char *data;
	// texture size
	unsigned int width;
	unsigned int height;
	// OpenGL texture handle
	GLuint glTextureLocation;
	// GLSL texture handle (uniform access in the shader)
	GLint uniformLocation;
	// GLSL handles to boolean variable (allows to toggle texture in shader) (optional)
	GLint uniformEnabledLocation;
};
// #INFO# array to store textures //
std::map<std::string, Texture> textures;
// method to load a texture from a given file
void createEmptyTexture(std::string texID, unsigned int width, unsigned int height);
void createTextureFromFile(std::string texID, std::string fileName);
void loadTextureData(const char *fileName, Texture &texture);
// method to initialize the texture object
void initTextures();


// window controls //
void updateGL();
void idle();
void keyboardEvent(unsigned char key, int x, int y);
void mouseEvent(int button, int state, int x, int y);
void mouseMoveEvent(int x, int y);

// camera controls //
CameraController camera(0, M_PI/4, 10);

// viewport //
GLint windowWidth, windowHeight;

// geometry //
void initScene();
void renderScene();

// OBJ import //
ObjLoader objLoader;
// local meshes //
MeshObj *screenQuad = NULL;

// #INFO# //
// FBO handle //
GLuint fbo;
GLuint rb;

int CheckGLErrors() {
	int errCount = 0;
	for(GLenum currError = glGetError(); currError != GL_NO_ERROR; currError = glGetError()) {
		std::stringstream sstr;

		switch (currError) {
			case GL_INVALID_ENUM : sstr << "GL_INVALID_ENUM"; break;
			case GL_INVALID_VALUE : sstr << "GL_INVALID_VALUE"; break;
			case GL_INVALID_OPERATION : sstr << "GL_INVALID_OPERATION"; break;
			case GL_INVALID_FRAMEBUFFER_OPERATION : sstr << "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
			case GL_OUT_OF_MEMORY : sstr << "GL_OUT_OF_MEMORY"; break;
			default : sstr << "unknown error (" << currError << ")";
		}
		std::cout << "found error: " << sstr.str() << std::endl;
		++errCount;
	}

	return errCount;
}

bool useDeferredShading;

int main (int argc, char **argv) {
	useDeferredShading = false;
	if (argc > 1) {
		if (atoi(argv[1]) > 0) useDeferredShading = true;
	}

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	glutInitContextVersion(3,3);
	glutInitContextFlags(GLUT_FORWARD_COMPATIBLE);
	glutInitContextProfile(GLUT_CORE_PROFILE);

	windowWidth = 512;
	windowHeight = 512;
	glutInitWindowSize(windowWidth, windowHeight);
	glutInitWindowPosition(100, 100);
	glutCreateWindow("Exercise 09 - Deferred Shading");

	glutDisplayFunc(updateGL);
	glutIdleFunc(idle);
	glutKeyboardFunc(keyboardEvent);
	glutMouseFunc(mouseEvent);
	glutMotionFunc(mouseMoveEvent);

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (GLEW_OK != err) {
		std::cout << "(glewInit) - Error: " << glewGetErrorString(err) << std::endl;
	}
	std::cout << "(glewInit) - Using GLEW " << glewGetString(GLEW_VERSION) << std::endl;

	// init stuff //
	initGL();

	// init matrix stacks with identity //
	glm_ProjectionMatrix.push(glm::mat4(1));
	glm_ModelViewMatrix.push(glm::mat4(1));

	initShader();
	initTextures();
	if (useDeferredShading) {
		initFBO();
	}
	initScene();

	// start render loop //
	if (enableShader()) {
		glutMainLoop();
		disableShader();

		// clean up allocated data //
		deleteShader();
	}

	return 0;
}

void initGL() {
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glEnable(GL_DEPTH_TEST);
}

std::string getUniformStructLocStr(const std::string &structName, const std::string &memberName, int arrayIndex = -1) {
	std::stringstream sstr("");
	sstr << structName;
	if (arrayIndex >= 0) {
		sstr << "[" << arrayIndex << "]";
	}
	sstr << "." << memberName;
	return sstr.str();
}

bool loadShaderCode(const char* vertProgramCode, GLuint &vertProgram, const char* fragmentProgramCode, GLuint &fragProgram) {
	vertProgram = loadShaderFile(vertProgramCode, GL_VERTEX_SHADER);
	fragProgram = loadShaderFile(fragmentProgramCode, GL_FRAGMENT_SHADER);

	if (vertProgram == 0) {
		std::cout << "(initShader) - Could not create vertex shader." << std::endl;
		deleteShader();
		return false;
	}
	if (fragProgram == 0) {
		std::cout << "(initShader) - Could not create fragment shader." << std::endl;
		deleteShader();
		return false;
	}
	return true;
}

bool attachAndLink(GLuint shaderProgram, GLuint vertexProgram, GLuint fragmentProgram) {
	// successfully loaded and compiled shaders -> attach them to program //
	glAttachShader(shaderProgram, vertexProgram);
	glAttachShader(shaderProgram, fragmentProgram);

	// mark shaders for deletion after clean up (they will be deleted, when detached from all shader programs) //
	glDeleteShader(vertexProgram);
	glDeleteShader(fragmentProgram);

	// link shader program //
	glLinkProgram(shaderProgram);

	// get log //
	int logMaxLength;
	glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &logMaxLength);
	char log[logMaxLength];
	int logLength = 0;
	glGetProgramInfoLog(shaderProgram, logMaxLength, &logLength, log);
	if (logLength > 0) {
		std::cout << "(initShader) - Linker log:\n------------------\n" << log << "\n------------------" << std::endl;
		return false;
	}

	return true;
}

GLuint createShader(const char* vertexProgramCode, const char* fragmentProgramCode) {
	GLuint program = 0;

	program = glCreateProgram();
	// check if operation failed //
	if (program == 0) {
		std::cout << "(initShader) - Failed creating shader program." << std::endl;
		return 0;
	}

	GLuint vertexShader = 0;
	GLuint fragmentShader = 0;
	if (!loadShaderCode(vertexProgramCode, vertexShader, fragmentProgramCode, fragmentShader)) {
		glDeleteProgram(program);
		return 0;
	}

	if (!attachAndLink(program, vertexShader, fragmentShader)) {
		glDeleteProgram(program);
		return 0;
	}

	return program;
}

inline void printTexLoc(std::string texID) {
	std::cout << "uniform location of tex \"" << texID << "\" is now: " << textures[texID].uniformLocation << std::endl;
}

void initShader() {
	if (!useDeferredShading) {
		shaderProgram = createShader("../shader/normal_mapping.vert", "../shader/normal_mapping.frag");
		// check if operation failed //
		if (shaderProgram == 0) {
			std::cout << "(initShader) - Failed creating shader program." << std::endl;
			return;
		}

		// set address of fragment color output //
		glBindFragDataLocation(shaderProgram, 0, "color");

		// get uniform locations for common variables //
		uniformLocations["projection"] = glGetUniformLocation(shaderProgram, "projection");
		uniformLocations["modelview"] = glGetUniformLocation(shaderProgram, "modelview");
		uniformLocations["view"] = glGetUniformLocation(shaderProgram, "view");

		// material unform locations //
		uniformLocations["material.ambient"] = glGetUniformLocation(shaderProgram, "material.ambient_color");
		uniformLocations["material.diffuse"] = glGetUniformLocation(shaderProgram, "material.diffuse_color");
		uniformLocations["material.specular"] = glGetUniformLocation(shaderProgram, "material.specular_color");
		uniformLocations["material.shininess"] = glGetUniformLocation(shaderProgram, "material.specular_shininess");

		// store the uniform locations for all light source properties
		for (int i = 0; i < 10; ++i) {
			UniformLocation_Light lightLocation;
			lightLocation.ambient_color = glGetUniformLocation(shaderProgram, getUniformStructLocStr("lightSource", "ambient_color", i).c_str());
			lightLocation.diffuse_color = glGetUniformLocation(shaderProgram, getUniformStructLocStr("lightSource", "diffuse_color", i).c_str());
			lightLocation.specular_color = glGetUniformLocation(shaderProgram, getUniformStructLocStr("lightSource", "specular_color", i).c_str());
			lightLocation.power = glGetUniformLocation(shaderProgram, getUniformStructLocStr("lightSource", "power", i).c_str());
			lightLocation.position = glGetUniformLocation(shaderProgram, getUniformStructLocStr("lightSource", "position", i).c_str());

			std::stringstream sstr("");
			sstr << "light_" << i;
			uniformLocations_Lights[sstr.str()] = lightLocation;
		}
		uniformLocations["usedLightCount"] = glGetUniformLocation(shaderProgram, "usedLightCount");

		// assign uniform locations to existing texture objects //
		textures["diffuse"].uniformLocation = glGetUniformLocation(shaderProgram, "diffuseTexture");
		textures["normal"].uniformLocation = glGetUniformLocation(shaderProgram, "normalMap");
	} else {
		// #INFO# load two shader programs and initialize uniform locations //

		// first pass //
		shaderPass[0] = createShader("../shader/deferred_pass1.vert", "../shader/deferred_pass1.frag");
		// check if operation failed //
		if (shaderPass[0] == 0) {
			std::cout << "(initShader) - Failed creating shader program 1." << std::endl;
			return;
		}
		// second pass //
		shaderPass[1] = createShader("../shader/deferred_pass2.vert", "../shader/deferred_pass2.frag");
		// check if operation failed //
		if (shaderPass[1] == 0) {
			std::cout << "(initShader) - Failed creating shader program 2." << std::endl;
			return;
		}

		// TODO?: set output format for both shaders (hint: glBindFragDataLocation) //
		glBindFragDataLocation(shaderPass[0], 0, "vertex_pos");
		glBindFragDataLocation(shaderPass[0], 1, "vertex_normal");
		glBindFragDataLocation(shaderPass[0], 2, "vertex_texcoord");

		glBindFragDataLocation(shaderPass[1], 0, "color");

		// get uniform locations for each shader //
		glUseProgram(shaderPass[0]);
		// pass 0 - vertex //
		uniformLocations["projection"] = glGetUniformLocation(shaderPass[0], "projection");
		uniformLocations["modelview"] = glGetUniformLocation(shaderPass[0], "modelview");
		// pass 0 - fragment //
		textures["normal"].uniformLocation = glGetUniformLocation(shaderPass[0], "normalMap");

		// pass 1 - vertex //
		glUseProgram(shaderPass[1]);
		uniformLocations["projection_p1"] = glGetUniformLocation(shaderPass[1], "projection");
		uniformLocations["modelview_p1"] = glGetUniformLocation(shaderPass[1], "modelview");
		// #INFO# additional matrix -> modelview for light positions //
		uniformLocations["view_p1"] = glGetUniformLocation(shaderPass[1], "view");

		for (int i = 0; i < 10; ++i) {
			UniformLocation_Light lightLocation;
			lightLocation.ambient_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "ambient_color", i).c_str());
			lightLocation.diffuse_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "diffuse_color", i).c_str());
			lightLocation.specular_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "specular_color", i).c_str());
			lightLocation.power = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "power", i).c_str());
			lightLocation.position = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "position", i).c_str());

			std::stringstream sstr("");
			sstr << "light_" << i;
			uniformLocations_Lights[sstr.str()] = lightLocation;
		}
		uniformLocations["usedLightCount"] = glGetUniformLocation(shaderPass[1], "usedLightCount");;
		// pass 1 - fragment //
		textures["diffuse"].uniformLocation = glGetUniformLocation(shaderPass[1], "diffuseTexture");
		printTexLoc("diffuse");

		uniformLocations["material.ambient"] = glGetUniformLocation(shaderPass[1], "material.ambient_color");
		uniformLocations["material.diffuse"] = glGetUniformLocation(shaderPass[1], "material.diffuse_color");
		uniformLocations["material.specular"] = glGetUniformLocation(shaderPass[1], "material.specular_color");
		uniformLocations["material.shininess"] = glGetUniformLocation(shaderPass[1], "material.specular_shininess");

		// store the uniform locations for all light source properties
		for (int i = 0; i < 10; ++i) {
			UniformLocation_Light lightLocation;
			lightLocation.ambient_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "ambient_color", i).c_str());
			lightLocation.diffuse_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "diffuse_color", i).c_str());
			lightLocation.specular_color = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "specular_color", i).c_str());
			lightLocation.power = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "power", i).c_str());
			lightLocation.position = glGetUniformLocation(shaderPass[1], getUniformStructLocStr("lightSource", "position", i).c_str());

			std::stringstream sstr("");
			sstr << "light_" << i;
			uniformLocations_Lights[sstr.str()] = lightLocation;
		}
		uniformLocations["usedLightCount"] = glGetUniformLocation(shaderPass[1], "usedLightCount");
	}
}

bool enableShader(int pass) {
	if (!useDeferredShading) {
		if (shaderProgram > 0) {
			glUseProgram(shaderProgram);
		} else {
			std::cout << "(enableShader) - Shader program not initialized." << std::endl;
		}
		return shaderProgram > 0;
	} else {
		if (shaderPass[pass]) {
			glUseProgram(shaderPass[pass]);
		} else {
			std::cout << "(enableShader) - Shader program not initialized." << std::endl;
		}
		return shaderPass[pass] > 0;
	}
}

void disableShader() {
	glUseProgram(0);
}

void deleteShader() {
	// use standard pipeline //
	glUseProgram(0);
	// delete shader program //
	if (!useDeferredShading) {
		glDeleteProgram(shaderProgram);
		shaderProgram = 0;
	} else {
		for (int i = 0; i < 2; ++i) {
			glDeleteProgram(shaderPass[i]);
			shaderPass[i] = 0;
		}
	}
}

// load and compile shader code //
char* loadShaderSource(const char* fileName) {
	char *shaderSource = NULL;

	std::ifstream file(fileName, std::ios::in);
	if (file.is_open()) {
		unsigned long srcLength = 0;
		file.tellg();
		file.seekg(0, std::ios::end);
		srcLength = file.tellg();
		file.seekg(0, std::ios::beg);
		shaderSource = new char[srcLength+1];
		file.read(shaderSource, srcLength);
		shaderSource[srcLength] = '\0';
		file.close();
	} else {
		std::cout << "(loadShaderSource) - Could not open file \"" << fileName << "\"." << std::endl;
	}

	return shaderSource;
}

// loads a source file and directly compiles it to a shader of 'shaderType' //
GLuint loadShaderFile(const char* fileName, GLenum shaderType) {
	GLuint shader = glCreateShader(shaderType);
	// check if operation failed //
	if (shader == 0) {
		std::cout << "(loadShaderFile) - Could not create shader." << std::endl;
		return 0;
	}

	// load source code from file //
	const char* shaderSrc = loadShaderSource(fileName);
	if (shaderSrc == NULL) return 0;
	// pass source code to new shader object //
	glShaderSource(shader, 1, (const char**)&shaderSrc, NULL);
	delete[] shaderSrc;
	// compile shader //
	glCompileShader(shader);

	// log compile messages, if any //
	int logMaxLength;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logMaxLength);
	char log[logMaxLength];
	int logLength = 0;
	glGetShaderInfoLog(shader, logMaxLength, &logLength, log);
	if (logLength > 0) {
		std::cout << "(loadShaderFile) - Compiler log:\n------------------\n" << log << "\n------------------" << std::endl;
	}

	// return compiled shader (may have compiled WITH errors) //
	return shader;
}

// TODO?: complete code to generate empty textures //
// hint: use GL_RGBA32F as texture format, GL_RGBA as internal format and GL_FLOAT as internal type
void createEmptyTexture(std::string texID, unsigned int width, unsigned int height) {
	Texture &texture = textures[texID];
	texture.width = width;
	texture.height = height;

	// TODO?: generate a texture //
	glGenTextures( 1, &texture.glTextureLocation);

	// TODO?: bind the texture and set wrapping and filtering parameters (use GL_NEAREST for filtering) //
	glBindTexture(GL_TEXTURE_2D, texture.glTextureLocation);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

	// TODO?: initialize the texture object without uploading data //
	texture.data = NULL;
	texture.isInitialized = true;
}

// #INFO# creates a texture by loading image data from disk //
void createTextureFromFile(std::string texID, std::string fileName) {
	Texture &texture = textures[texID];
	loadTextureData(fileName.c_str(), texture);
	if (texture.data != NULL) {
		// texture has been successfully loaded //
		glGenTextures(1, &texture.glTextureLocation);

		glBindTexture(GL_TEXTURE_2D, texture.glTextureLocation);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture.width, texture.height, 0, GL_BGR, GL_UNSIGNED_BYTE, texture.data);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);

		// clean up local texture data //
		delete[] texture.data;
	}
}

// loads texture data from disk //
void loadTextureData(const char *textureFile, Texture &texture) {
	IplImage *image = cvLoadImage(textureFile, CV_LOAD_IMAGE_COLOR);
	if (image != NULL) {
		// flip image vertically //
		cvFlip(image);
		texture.width = image->width;
		texture.height = image->height;
		if (texture.data) {
			delete[] texture.data;
			texture.data = NULL;
		}
		texture.data = new unsigned char[image->imageSize];
		memcpy(texture.data, image->imageData, image->imageSize);
		texture.isInitialized = true;
	} else {
		texture.isInitialized = false;
		std::cout << "(loadTextureData) : reading from \"" << textureFile << "\" failed." << std::endl;
	}
	cvReleaseImage(&image);
}

// #INFO# loads neccessary textures from disk //
void initTextures (void) {
	createTextureFromFile("diffuse", "../textures/diffuse.jpg");
	createTextureFromFile("normal", "../textures/normals.jpg");
}

void initScene() {
	camera.setFar(1000.0f);

	// load scene.obj from disk and create renderable MeshObj //
	objLoader.loadObjFile("../meshes/head.obj", "sceneObject");

	// init materials //
	Material mat;
	mat.ambient_color = glm::vec3(1.0, 1.0, 1.0);
	mat.diffuse_color = glm::vec3(1.0, 1.0, 1.0);
	mat.specular_color = glm::vec3(1.0, 1.0, 1.0);
	mat.specular_shininess = 5.0;
	materials.push_back(mat);

	// save material count for later and select first material //
	materialCount = materials.size();
	materialIndex = 0;

	// init lights //
	LightSource light;
	light.ambient_color = glm::vec3(0.15, 0.15, 0.15);
	light.diffuse_color = glm::vec3(1.0, 1.0, 1.0);
	light.specular_color = glm::vec3(1.0, 1.0, 1.0);
	light.power = 0.25f;

	// create circle of lights //
	int numLights = 10;
	float angleStep = 2.0f * M_PI / numLights;
	float angle = 0.0f;
	for (int i = 0; i < numLights; ++i, angle += angleStep) {
		light.position = glm::vec3(5 * sin(angle), 3, 5 * cos(angle));
		lights.push_back(light);
	}

	// save light source count for later and select first light source //
	lightCount = lights.size();
}

// TODO?: initialize your FBO here //
void initFBO() {
	// TODO? :generate FBO and depthBuffer //
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	// TODO?: generate texture objects to hold the data //
	createEmptyTexture("def_vertexMap", windowWidth, windowHeight);
	createEmptyTexture("def_normalMap", windowWidth, windowHeight);
	createEmptyTexture("def_texCoordMap", windowWidth, windowHeight);

	// TODO?: get uniforms locations in shader (pass 0) //
	textures["def_vertexMap"].uniformLocation = glGetUniformLocation(shaderPass[1], "def_vertexMap");
	textures["def_normalMap"].uniformLocation = glGetUniformLocation(shaderPass[1], "def_normalMap");
	textures["def_texCoordMap"].uniformLocation = glGetUniformLocation(shaderPass[1], "def_texCoordMap");

	// TODO?: attach textures to FBO for output VERTEX, NORMAL, TEXCOORD //
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures["def_vertexMap"].glTextureLocation, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, textures["def_normalMap"].glTextureLocation, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, textures["def_texCoordMap"].glTextureLocation, 0);

	// TODO?: generate renderbuffer for depth data //
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, windowWidth, windowHeight);

	// TODO?: attach depth renderbuffer //
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);

	// TODO?: unbind FBO until it's needed //
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

// #INFO# uploads light and material to current shader //
void setupLightAndMaterial() {
	// uploads the properties of the currently active light sources here //
	int shaderLightIdx = 0;
	for (unsigned int i = 0; i < lightCount; ++i) {
		if (lights[i].enabled) {
			std::stringstream sstr("");
			sstr << "light_" << shaderLightIdx;
			UniformLocation_Light &light = uniformLocations_Lights[sstr.str()];
			glUniform3fv(light.position, 1, glm::value_ptr(lights[i].position));
			glUniform3fv(light.ambient_color, 1, glm::value_ptr(lights[i].ambient_color));
			glUniform3fv(light.diffuse_color, 1, glm::value_ptr(lights[i].diffuse_color));
			glUniform3fv(light.specular_color, 1, glm::value_ptr(lights[i].specular_color));
			glUniform1f(light.power, lights[i].power);
			++shaderLightIdx;
		}
	}
	glUniform1i(uniformLocations["usedLightCount"], shaderLightIdx);

	// uploads the chosen material properties here //
	glUniform3fv(uniformLocations["material.ambient"], 1, glm::value_ptr(materials[materialIndex].ambient_color));
	glUniform3fv(uniformLocations["material.diffuse"], 1, glm::value_ptr(materials[materialIndex].diffuse_color));
	glUniform3fv(uniformLocations["material.specular"], 1, glm::value_ptr(materials[materialIndex].specular_color));
	glUniform1f(uniformLocations["material.shininess"], materials[materialIndex].specular_shininess);
}

// #INFO# creates a screen filling quad as a new MeshObj (stored in screenQuad) //
void initScreenFillingQuad(void) {
	screenQuad = new MeshObj();
	MeshData mesh;
	std::vector<glm::vec3> vertices;
	std::vector<glm::vec2> texCoords;
	std::vector<int> indices;

	// geometry //
	vertices.push_back(glm::vec3(0, 0, 0));
	texCoords.push_back(glm::vec2(0, 0));
	vertices.push_back(glm::vec3(1, 0, 0));
	texCoords.push_back(glm::vec2(1, 0));
	vertices.push_back(glm::vec3(1, 1, 0));
	texCoords.push_back(glm::vec2(1, 1));
	vertices.push_back(glm::vec3(0, 1, 0));
	texCoords.push_back(glm::vec2(0, 1));

	// two triangles //
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	indices.push_back(0);
	indices.push_back(2);
	indices.push_back(3);


	for (std::vector<glm::vec3>::iterator vertex = vertices.begin(); vertex != vertices.end(); ++vertex) {
		mesh.vertex_position.push_back(vertex->x);
		mesh.vertex_position.push_back(vertex->y);
		mesh.vertex_position.push_back(vertex->z);
	}
	for (std::vector<glm::vec2>::iterator texCoord = texCoords.begin(); texCoord != texCoords.end(); ++texCoord) {
		mesh.vertex_texcoord.push_back(texCoord->x);
		mesh.vertex_texcoord.push_back(texCoord->y);
	}

	for (std::vector<int>::iterator index = indices.begin(); index != indices.end(); ++index) {
		mesh.indices.push_back(*index);
	}

	screenQuad->setData(mesh);
}

// TODO?: complete the code of the deferred shading  pipeline //
void renderScene() {
	if (!useDeferredShading) {
		glUseProgram(shaderProgram);
		// upload view matrix //
		glUniformMatrix4fv(uniformLocations["view"], 1, false, glm::value_ptr(glm_ModelViewMatrix.top()));
		// setup light and material in shader //
		setupLightAndMaterial();

		// upload textures to individual texture units //
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures["diffuse"].glTextureLocation);
		glUniform1i(textures["diffuse"].uniformLocation, 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textures["normal"].glTextureLocation);
		glUniform1i(textures["normal"].uniformLocation, 1);

		for (int y = -10; y < 11; ++y) {
			for (int x = -10; x < 11; ++x) {
				glm_ModelViewMatrix.push(glm_ModelViewMatrix.top());
				glm_ModelViewMatrix.top() *= glm::translate(glm::vec3(x, 0, y));
				glm_ModelViewMatrix.top() *= glm::scale(glm::vec3(2));

				glUniformMatrix4fv(uniformLocations["modelview"], 1, false, glm::value_ptr(glm_ModelViewMatrix.top()));

				// render the actual object //
				MeshObj *mesh = objLoader.getMeshObj("sceneObject");
				mesh->render();

				// restore scene graph to previous state //
				glm_ModelViewMatrix.pop();
			}
		}
	} else {
		// TODO?: pass 0 -> render scene to FBO //
		// - enable pass 0 shader   //
		// - bind FBO render target //
		// - render without lights  //

		glUseProgram(shaderPass[0]);
		// TODO?: bind FBO for off screen rendering //
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		// TODO?: select correct draw buffers //
		GLenum buffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
		glDrawBuffers(3, buffers);

		// upload normal textures //
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures["normal"].glTextureLocation);
		glUniform1i(textures["normal"].uniformLocation, 0);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// place and render model geometry //
		for (int y = -10; y < 11; ++y) {
			for (int x = -10; x < 11; ++x) {
				glm_ModelViewMatrix.push(glm_ModelViewMatrix.top());
				glm_ModelViewMatrix.top() *= glm::translate(glm::vec3(x, 0, y));
				glm_ModelViewMatrix.top() *= glm::scale(glm::vec3(2));

				glUniformMatrix4fv(uniformLocations["modelview"], 1, false, glm::value_ptr(glm_ModelViewMatrix.top()));

				// render the actual object //
				MeshObj *mesh = objLoader.getMeshObj("sceneObject");
				mesh->render();

				// restore scene graph to previous state //
				glm_ModelViewMatrix.pop();
			}
		}

		// TODO?: pass 1 : -> render quad to screen //
		// - enable pass 1 shader            //
		glUseProgram(shaderPass[1]);
		// - bind standard frame buffer -> 0 //
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// create simple projection and view matrices //
		glm::mat4 pass1_proj = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);
		glm::mat4 pass1_modelview = glm::mat4(1);

		// upload transformation matrices matrix //
		glUniformMatrix4fv(uniformLocations["projection_p1"], 1, false, glm::value_ptr(pass1_proj));
		glUniformMatrix4fv(uniformLocations["modelview_p1"], 1, false, glm::value_ptr(pass1_modelview));
		// TODO?: upload the light modelview transformation as 'view' //
		glUniformMatrix4fv(uniformLocations["view_p1"], 1, false, glm::value_ptr(glm_ModelViewMatrix.top()));

		// setup light and material in shader //
		setupLightAndMaterial();

		// upload diffuse texture //
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures["diffuse"].glTextureLocation);
		glUniform1i(textures["diffuse"].uniformLocation, 0);

		// TODO?: upload textures created in previous render pass //

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textures["def_vertexMap"].glTextureLocation);
		glUniform1i(textures["def_vertexMap"].uniformLocation, 1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, textures["def_normalMap"].glTextureLocation);
		glUniform1i(textures["def_normalMap"].uniformLocation, 2);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, textures["def_texCoordMap"].glTextureLocation);
		glUniform1i(textures["def_texCoordMap"].uniformLocation, 3);

		// render screen filling quad //
		if (!screenQuad) initScreenFillingQuad();
		screenQuad->render();
	}
}

void updateGL() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// set viewport dimensions //
	glViewport(0, 0, windowWidth, windowHeight);

	// get projection mat from camera controller //
	glm_ProjectionMatrix.top() = camera.getProjectionMat();
	// upload projection matrix //
	glUniformMatrix4fv(uniformLocations["projection"], 1, false, glm::value_ptr(glm_ProjectionMatrix.top()));

	// init scene graph by cloning the top entry, which can now be manipulated //
	// get modelview mat from camera controller //
	glm_ModelViewMatrix.top() = camera.getModelViewMat();

	// render scene //
	renderScene();

	// swap renderbuffers for smooth rendering //
	glutSwapBuffers();
}

void idle() {
	glutPostRedisplay();
}

// toggles a light source on or off //
void toggleLightSource(unsigned int i) {
	if (i < lightCount) {
		lights[i].enabled = !lights[i].enabled;
	}
}

void keyboardEvent(unsigned char key, int x, int y) {
	switch (key) {
		case 'x':
		case 27 : {
				  exit(0);
				  break;
			  }
		case 'w': {
				  // move forward //
				  camera.move(CameraController::MOVE_FORWARD);
				  break;
			  }
		case 's': {
				  // move backward //
				  camera.move(CameraController::MOVE_BACKWARD);
				  break;
			  }
		case 'a': {
				  // move left //
				  camera.move(CameraController::MOVE_LEFT);
				  break;
			  }
		case 'd': {
				  // move right //
				  camera.move(CameraController::MOVE_RIGHT);
				  break;
			  }
		case 'z': {
				  camera.setOpeningAngle(camera.getOpeningAngle() + 0.1f);
				  break;
			  }
		case 'h': {
				  camera.setOpeningAngle(std::min(std::max(camera.getOpeningAngle() - 0.1f, 1.0f), 180.0f));
				  break;
			  }
		case 'r': {
				  camera.setNear(std::min(camera.getNear() + 0.1f, camera.getFar() - 0.01f));
				  break;
			  }
		case 'f': {
				  camera.setNear(std::max(camera.getNear() - 0.1f, 0.1f));
				  break;
			  }
		case 't': {
				  camera.setFar(camera.getFar() + 0.1f);
				  break;
			  }
		case 'g': {
				  camera.setFar(std::max(camera.getFar() - 0.1f, camera.getNear() + 0.01f));
				  break;
			  }
		case 'm': {
				  materialIndex++;
				  if (materialIndex >= materialCount) materialIndex = 0;
				  break;
			  }
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': {
				  int lightIdx;
				  std::stringstream keyStr;
				  keyStr << key;
				  keyStr >> lightIdx;
				  if (lightIdx == 0) lightIdx = 10;
				  if (lightIdx > 0) toggleLightSource(lightIdx - 1);
				  break;
			  }
	}
	glutPostRedisplay();
}

void mouseEvent(int button, int state, int x, int y) {
	CameraController::MouseState mouseState;
	if (state == GLUT_DOWN) {
		switch (button) {
			case GLUT_LEFT_BUTTON : {
							mouseState = CameraController::LEFT_BTN;
							break;
						}
			case GLUT_RIGHT_BUTTON : {
							 mouseState = CameraController::RIGHT_BTN;
							 break;
						 }
			default : break;
		}
	} else {
		mouseState = CameraController::NO_BTN;
	}
	camera.updateMouseBtn(mouseState, x, y);
	glutPostRedisplay();
}

void mouseMoveEvent(int x, int y) {
	camera.updateMousePos(x, y);
	glutPostRedisplay();
}

