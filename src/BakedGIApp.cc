#include "BakedGIApp.hh"
#include "LightMapWriter.hh"
#include "ProbeDataWriter.hh"
#include "ProbeDataReader.hh"

#include <glow/objects/Program.hh>
#include <glow/objects/Texture2D.hh>
#include <glow/objects/TextureRectangle.hh>
#include <glow/objects/TextureCubeMap.hh>
#include <glow/objects/Framebuffer.hh>
#include <glow/data/TextureData.hh>
#include <glow/common/str_utils.hh>
#include <glow/common/scoped_gl.hh>
#include <glow-extras/geometry/UVSphere.hh>
#include <glow-extras/geometry/Quad.hh>
#include <glow-extras/geometry/Cube.hh>
#include <glow-extras/camera/GenericCamera.hh>
#include <glow-extras/assimp/Importer.hh>
#include <glow-extras/debugging/DebugRenderer.hh>
#include <AntTweakBar.h>
#include <embree3/rtcore.h>
#include <fstream>

BakedGIApp::BakedGIApp(const std::string& gltfPath, const std::string& lmPath, const std::string& pdPath) {
	this->gltfPath = gltfPath;
	this->lmPath = lmPath;
	this->pdPath = pdPath;
}

void BakedGIApp::init() {
	glow::glfw::GlfwApp::init();
    
    GLint numLayers;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &numLayers);
    glow::info() << "Max array texture layers: " << numLayers;

	GLint maxUniforms;
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxUniforms);
	glow::info() << "Max fragment shader uniforms: " << maxUniforms;

	this->setQueryStats(false);
	this->setCameraMoveSpeed(8.0f);
	this->setCameraTurnSpeed(3.0f);

	auto cam = getCamera();
	cam->setPosition({ 0, 0, 1 });
	cam->setTarget({ 0, 0, 0 }, { 0, 1, 0 });

	scene.loadFromGltf(gltfPath);

	pipeline.reset(new RenderPipeline());
	pipeline->attachCamera(*getCamera());
	pipeline->attachLight(scene.getSun());

	debugPathTracer.reset(new DebugPathTracer());
	debugPathTracer->attachDebugCamera(*getCamera());
	scene.buildPathTracerScene(*debugPathTracer);
	scene.buildRealtimeObjects(lmPath);
    
	auto skybox = CubeMap::loadFromFiles(
		"textures/miramar/posx.jpg",
		"textures/miramar/negx.jpg",
		"textures/miramar/posy.jpg",
		"textures/miramar/negy.jpg",
		"textures/miramar/posz.jpg",
		"textures/miramar/negz.jpg");
	debugPathTracer->setBackgroundCubeMap(skybox);

	TwAddVarRW(tweakbar(), "Use Direct Lighting", TW_TYPE_BOOLCPP, &useDirectLighting, "group=light");
	TwAddVarRW(tweakbar(), "Light Color", TW_TYPE_COLOR3F, &scene.getSun().color, "group=light");
	TwAddVarRW(tweakbar(), "Light Power", TW_TYPE_FLOAT, &scene.getSun().power, "group=light min=0.0 step=0.1");
	TwAddVarRW(tweakbar(), "Light Dir", TW_TYPE_DIR3F, &scene.getSun().direction, "group=light");
	TwAddVarRW(tweakbar(), "Shadow Map Size", TwDefineEnum("", nullptr, 0), &shadowMapSize,
		"group=light, enum='64 {64}, 128 {128}, 256 {256}, 512 {512}, 1024 {1024}, 2048 {2048}, 4096 {4096}'");
	TwAddVarRW(tweakbar(), "Shadow Map Offset", TW_TYPE_FLOAT, &shadowMapOffset, "group=light step=0.0001");

	TwAddVarRW(tweakbar(), "Bloom %", TW_TYPE_FLOAT, &bloomPercentage, "group=postprocess min=0.0 max =1.0 step=0.01");
	TwAddVarRW(tweakbar(), "Exposure", TW_TYPE_FLOAT, &exposureAdjustment, "group=postprocess min=0.0 step=0.1");

	TwAddButton(tweakbar(), "Debug Trace", debugTrace, &sharedData, "group=pathtrace");
	TwAddButton(tweakbar(), "Save Trace", saveTrace, &sharedData, "group=pathtrace");
	TwAddVarRW(tweakbar(), "Show Debug Image", TW_TYPE_BOOLCPP, &showDebugImage, "group=pathtrace");
	TwAddVarRW(tweakbar(), "Debug Trace Scale", TW_TYPE_FLOAT, &debugTraceScale, "group=pathtrace step=0.01");
	TwAddVarRW(tweakbar(), "SPP", TW_TYPE_UINT32, &samplesPerPixel, "group=pathtrace");
	TwAddVarRW(tweakbar(), "Max Path Depth", TW_TYPE_UINT32, &maxPathDepth, "group=pathtrace");
	TwAddVarRW(tweakbar(), "Clamp Depth", TW_TYPE_UINT32, &clampDepth, "group=pathtrace");
	TwAddVarRW(tweakbar(), "Clamp Radiance", TW_TYPE_FLOAT, &clampRadiance, "group=pathtrace");

	TwAddVarRW(tweakbar(), "Show Lightmap", TW_TYPE_BOOLCPP, &showDebugLightMap, "group=lightmap");
	TwAddVarRW(tweakbar(), "Lightmap Index", TW_TYPE_INT32, &lightMapIndex, "group=lightmap min=0 step=1");
	TwAddVarRW(tweakbar(), "Use Irradiance Map", TW_TYPE_BOOLCPP, &useIrradianceMap, "group=lightmap");
	TwAddVarRW(tweakbar(), "Use AO Map", TW_TYPE_BOOLCPP, &useAOMap, "group=lightmap");
	TwAddVarRW(tweakbar(), "Probe Mip Level", TW_TYPE_INT32, &debugEnvMapMipLevel, "group=probes min=0");
	TwAddVarRW(tweakbar(), "Show Probes", TW_TYPE_BOOLCPP, &showDebugEnvProbes, "group=probes");
	TwAddVarRW(tweakbar(), "Show Debug Probe Vis Grid", TW_TYPE_BOOLCPP, &showProbeVisGrid, "group=probes");
	TwAddVarRW(tweakbar(), "Use IBL", TW_TYPE_BOOLCPP, &useIbl, "group=probes");
	TwAddVarRW(tweakbar(), "Use Local Probes", TW_TYPE_BOOLCPP, &useLocalProbes, "group=probes");

	TwAddVarRW(tweakbar(), "Enable Probe Placement", TW_TYPE_BOOLCPP, &sharedData.isInProbePlacementMode, "group=probeedit");
	TwAddButton(tweakbar(), "Place Probe", placeProbe, &sharedData, "group=probeedit");
	TwAddButton(tweakbar(), "Remove Probe", removeProbe, &sharedData, "group=probeedit");
	TwAddVarRW(tweakbar(), "Probe Index", TW_TYPE_INT32, &sharedData.currentProbeIndex, "group=probeedit min=-1");
	TwAddVarRW(tweakbar(), "Probe Pos X", TW_TYPE_FLOAT, &sharedData.currentProbePos.x, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe Pos Y", TW_TYPE_FLOAT, &sharedData.currentProbePos.y, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe Pos Z", TW_TYPE_FLOAT, &sharedData.currentProbePos.z, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMin X", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMin.x, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMin Y", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMin.y, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMin Z", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMin.z, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMax X", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMax.x, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMax Y", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMax.y, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Probe BoxMax Z", TW_TYPE_FLOAT, &sharedData.currentProbeAABBMax.z, "group=probeedit step=0.01");
	TwAddVarRW(tweakbar(), "Num Bounces", TW_TYPE_INT32, &sharedData.numBounces, "group=probeedit min=1");
	TwAddVarRW(tweakbar(), "Texture Size", TwDefineEnum("", nullptr, 0), &sharedData.probeSize, 
		"group=probeedit enum='16 {16}, 32 {32}, 64 {64}, 128 {128}, 256 {256}, 512 {512}, 1024 {1024}'");
	TwAddVarRW(tweakbar(), "Voxel Grid Res", TwDefineEnum("", nullptr, 0), &sharedData.voxelGridRes,
		"group=probeedit enum='16 {16}, 32 {32}, 64 {64}, 128 {128}, 256 {256}, 512 {512}, 1024 {1024}'");
	TwAddButton(tweakbar(), "Rebake Probes", rebakeProbes, &sharedData, "group=probeedit");
	TwAddButton(tweakbar(), "Save Probe Data", saveProbeData, &sharedData, "group=probeedit");

	// Set the shared data for the tweakbar actions
	sharedData.camera = getCamera();
	sharedData.pipeline = pipeline.get();
	sharedData.pathTracer = debugPathTracer.get();
	sharedData.scene = &scene;
	sharedData.probes = &reflectionProbes;

	if (!pdPath.empty()) {
		sharedData.visibilityGrid = readProbeDataToFile(pdPath, *sharedData.probes, sharedData.probeSize, sharedData.numBounces);
		pipeline->setProbeVisibilityGrid(*sharedData.visibilityGrid);
		pipeline->setReflectionProbes(*sharedData.probes);
		pipeline->bakeReflectionProbes(*sharedData.probes, sharedData.probeSize, sharedData.numBounces, scene.getMeshes());
	}
}

void BakedGIApp::render(float elapsedSeconds) {
    if (glm::abs(lastDebugTraceScale - debugTraceScale) > 0.001f) {
        debugPathTracer->setDebugImageSize(
            static_cast<int>(getCamera()->getViewportWidth() * debugTraceScale),
            static_cast<int>(getCamera()->getViewportHeight() * debugTraceScale)
        );
        lastDebugTraceScale = debugTraceScale;
    }

	if (useDirectLighting && directLightingFade < 1.0f) {
		directLightingFade = std::min(1.0f, directLightingFade + elapsedSeconds * fadeSpeed);
	}
	else if (!useDirectLighting && directLightingFade > 0.0f) {
		directLightingFade = std::max(0.0f, directLightingFade - elapsedSeconds * fadeSpeed);
	}

	if (useIrradianceMap && irraddianceMapFade < 1.0f) {
		irraddianceMapFade = std::min(1.0f, irraddianceMapFade + elapsedSeconds * fadeSpeed);
	}
	else if (!useIrradianceMap && irraddianceMapFade > 0.0f) {
		irraddianceMapFade = std::max(0.0f, irraddianceMapFade - elapsedSeconds * fadeSpeed);
	}

	if (useIbl && iblFade < 1.0f) {
		iblFade = std::min(1.0f, iblFade + elapsedSeconds * fadeSpeed);
	}
	else if (!useIbl && iblFade > 0.0f) {
		iblFade = std::max(0.0f, iblFade - elapsedSeconds * fadeSpeed);
	}

	if (useLocalProbes && localProbesFade < 1.0f) {
		localProbesFade = std::min(1.0f, localProbesFade + elapsedSeconds * fadeSpeed);
	}
	else if (!useLocalProbes && localProbesFade > 0.0f) {
		localProbesFade = std::max(0.0f, localProbesFade - elapsedSeconds * fadeSpeed);
	}

	if (lastProbeIndex != sharedData.currentProbeIndex) {
		if (sharedData.currentProbeIndex >= 0 && sharedData.currentProbeIndex < reflectionProbes.size()) {
			sharedData.currentProbeAABBMin = reflectionProbes[sharedData.currentProbeIndex].aabbMin;
			sharedData.currentProbeAABBMax = reflectionProbes[sharedData.currentProbeIndex].aabbMax;
			sharedData.currentProbePos = reflectionProbes[sharedData.currentProbeIndex].position;
		}
		lastProbeIndex = sharedData.currentProbeIndex;
	}
	
	if (sharedData.currentProbeIndex >= 0 && sharedData.currentProbeIndex < reflectionProbes.size()) {
		reflectionProbes[sharedData.currentProbeIndex].aabbMin = sharedData.currentProbeAABBMin;
		reflectionProbes[sharedData.currentProbeIndex].aabbMax = sharedData.currentProbeAABBMax;
		reflectionProbes[sharedData.currentProbeIndex].position = sharedData.currentProbePos;
	}
    
	debugPathTracer->setSamplesPerPixel(samplesPerPixel);
	debugPathTracer->setMaxPathDepth(maxPathDepth);
	debugPathTracer->setClampDepth(clampDepth);
	debugPathTracer->setClampRadiance(clampRadiance);
	pipeline->setDebugTexture(showDebugImage ? debugPathTracer->getDebugTexture() : nullptr, DebugImageLocation::BottomRight);
	lightMapIndex = std::min(lightMapIndex, (int) scene.getMeshes().size() - 1);
	pipeline->setDebugTexture(showDebugLightMap ? scene.getMeshes()[lightMapIndex].material.lightMap : nullptr, DebugImageLocation::TopRight);
	pipeline->setShadowMapSize(shadowMapSize);
	pipeline->setShadowMapOffset(shadowMapOffset);
	//pipeline->setUseIrradianceMap(useIrradianceMap);
	//pipeline->setUseAOMap(useAOMap);
	//pipeline->setUseIBL(useIbl);
	//pipeline->setUseLocalProbes(useLocalProbes);
	pipeline->setBloomPercentage(bloomPercentage);
	pipeline->setExposureAdjustment(exposureAdjustment);
	pipeline->setDebugEnvMapMipLevel(debugEnvMapMipLevel);
	pipeline->setDebugReflProbeGridEnabled(showDebugEnvProbes);
	pipeline->setShowDebugProbeVisGrid(showProbeVisGrid);
	pipeline->setCurrentProbeIndex(sharedData.currentProbeIndex);
	pipeline->setProbePlancementPreview(sharedData.isInProbePlacementMode, getCamera()->getPosition() + getCamera()->getForwardDirection());
	pipeline->setReflectionProbes(reflectionProbes);
	pipeline->setFadeValues(directLightingFade, irraddianceMapFade, iblFade, localProbesFade);
	scene.render(*pipeline);
}

void BakedGIApp::onResize(int w, int h) {
	glow::glfw::GlfwApp::onResize(w, h);
	pipeline->resizeBuffers(w, h);
}

bool BakedGIApp::onKey(int key, int scancode, int action, int mods) {
	if (GlfwApp::onKey(key, scancode, action, mods)) {
		return true;
	}

	if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
		setCursorMode(getCursorMode() == glow::glfw::CursorMode::Normal ? glow::glfw::CursorMode::Hidden : glow::glfw::CursorMode::Normal);
		return true;
	}
	if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
		useDirectLighting = !useDirectLighting;
		return true;
	}
	if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
		useIrradianceMap = !useIrradianceMap;
		return true;
	}
	if (key == GLFW_KEY_4 && action == GLFW_PRESS) {
		useIbl = !useIbl;
		return true;
	}
	if (key == GLFW_KEY_5 && action == GLFW_PRESS) {
		useLocalProbes = !useLocalProbes;
		return true;
	}
	if (key == GLFW_KEY_6 && action == GLFW_PRESS) {
		showDebugEnvProbes = !showDebugEnvProbes;
		return true;
	}
	if (key == GLFW_KEY_7 && action == GLFW_PRESS) {
		showProbeVisGrid = !showProbeVisGrid;
		return true;
	}

	return false;
}

void TW_CALL BakedGIApp::debugTrace(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);
	sharedData->pathTracer->traceDebugImage();
}

void TW_CALL BakedGIApp::saveTrace(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);
	sharedData->pathTracer->saveDebugImageToFile("debugtrace.png");
}

void TW_CALL BakedGIApp::placeProbe(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);
	if (sharedData->isInProbePlacementMode) {
		ReflectionProbe probe;
		probe.position = sharedData->camera->getPosition() + sharedData->camera->getForwardDirection();
		probe.layer = static_cast<int>(sharedData->probes->size());
		probe.aabbMin = glm::vec3(-2);
		probe.aabbMax = glm::vec3(2);
		sharedData->probes->push_back(probe);
		sharedData->currentProbeIndex = static_cast<int>(sharedData->probes->size() - 1);
	}
}

void TW_CALL BakedGIApp::removeProbe(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);
	if (sharedData->currentProbeIndex >= 0 && sharedData->currentProbeIndex < sharedData->probes->size()) {
		for (int i = sharedData->currentProbeIndex + 1; i < sharedData->probes->size(); ++i) {
			(*sharedData->probes)[i].layer--;
		}
		sharedData->probes->erase(sharedData->probes->begin() + sharedData->currentProbeIndex);
	}
}

// Returns the squared distance between a point p and an AABB b
float sqDistPointAABB(glm::vec3 point, glm::vec3 aabbMin, glm::vec3 aabbMax) {
	float sqDist = 0.0f;
	for (int i = 0; i < 3; i++) {
		float v = point[i];
		if (v < aabbMin[i]) sqDist += (aabbMin[i] - v) * (aabbMin[i] - v);
		if (v > aabbMax[i]) sqDist += (v - aabbMax[i]) * (v - aabbMax[i]);
	}
	return sqDist;
}
bool testSphereAABB(glm::vec3 spherePos, float radius, glm::vec3 aabbMin, glm::vec3 aabbMax) {
	float sqDist = sqDistPointAABB(spherePos, aabbMin, aabbMax);
	return sqDist <= radius * radius;
}

bool testBoxBox(glm::vec3 min0, glm::vec3 max0, glm::vec3 min1, glm::vec3 max1) {
	return min0.x < max1.x && min0.y < max1.y && min0.z < max1.z
		&& max0.x > min1.x && max0.y > min1.y && max0.z > min1.z;
}

void TW_CALL BakedGIApp::rebakeProbes(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);

	glm::vec3 min, max;
	sharedData->scene->getBoundingBox(min, max);

	sharedData->visibilityGrid = std::make_shared<VoxelGrid<glm::ivec3>>(min, max, glm::ivec3(sharedData->voxelGridRes));
	sharedData->visibilityGrid->fill(glm::ivec3(-1));
	sharedData->visibilityGrid->forEachVoxel([&](int x, int y, int z) {
		glm::vec3 center = sharedData->visibilityGrid->getVoxelCenter({ x, y, z });
		glm::vec3 voxelMin = sharedData->visibilityGrid->getVoxelMin({ x, y, z });
		glm::vec3 voxelMax = sharedData->visibilityGrid->getVoxelMax({ x, y, z });

		std::vector<int> visibleProbes;
		for (int probeIndex = 0; probeIndex < sharedData->probes->size(); ++probeIndex) {
			glm::vec3 probeMin = (*sharedData->probes)[probeIndex].position + (*sharedData->probes)[probeIndex].aabbMin;
			glm::vec3 probeMax = (*sharedData->probes)[probeIndex].position + (*sharedData->probes)[probeIndex].aabbMax;
			if (testBoxBox(voxelMin, voxelMax, probeMin, probeMax)) {
				visibleProbes.push_back(probeIndex);
			}
		}

		std::sort(visibleProbes.begin(), visibleProbes.end(), [&](int a, int b) {
			glm::vec3 distA = (*sharedData->probes)[a].position - center;
			glm::vec3 distB = (*sharedData->probes)[b].position - center;
			return glm::dot(distA, distA) < glm::dot(distB, distB);
		});

		glm::ivec3 layers(-1);
		if (visibleProbes.size() == 1) {
			layers = glm::ivec3((int)(*sharedData->probes)[visibleProbes[0]].layer);
		}
		else if (visibleProbes.size() == 2) {
			layers = glm::ivec3((int)(*sharedData->probes)[visibleProbes[0]].layer, (int)(*sharedData->probes)[visibleProbes[0]].layer, (int)(*sharedData->probes)[visibleProbes[1]].layer);
		}
		else if (visibleProbes.size() >= 3) {
			layers = glm::ivec3((int)(*sharedData->probes)[visibleProbes[0]].layer, (int)(*sharedData->probes)[visibleProbes[1]].layer, (int)(*sharedData->probes)[visibleProbes[2]].layer);
		}

		sharedData->visibilityGrid->setVoxel({ x, y, z }, layers);
	});

	sharedData->pipeline->setProbeVisibilityGrid(*sharedData->visibilityGrid);
	sharedData->pipeline->bakeReflectionProbes(*sharedData->probes, sharedData->probeSize, sharedData->numBounces, sharedData->scene->getMeshes());
	
}

void TW_CALL BakedGIApp::saveProbeData(void* clientData) {
	auto sharedData = static_cast<SharedData*>(clientData);
	writeProbeDataToFile("a.pd", *sharedData->probes, sharedData->probeSize, sharedData->numBounces, *sharedData->visibilityGrid);
}
