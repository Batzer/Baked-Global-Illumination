#pragma once

#include "Scene.hh"
#include "RenderPipeline.hh"
#include "DebugPathTracer.hh"
#include "LightMapBaker.hh"

#include <glm/ext.hpp>
#include <glow/fwd.hh>
#include <glow-extras/glfw/GlfwApp.hh>

#include <memory>

class BakedGIApp : public glow::glfw::GlfwApp {
protected:
	virtual void init() override;
	virtual void render(float elapsedSeconds) override;
	virtual void onResize(int w, int h) override;

private:
	Scene scene;
	std::unique_ptr<RenderPipeline> pipeline;
	std::unique_ptr<DebugPathTracer> debugPathTracer;
    std::unique_ptr<LightMapBaker> lightMapBaker;
	bool showDebugImage = false;
	float debugTraceScale = 0.5f;
	unsigned int samplesPerPixel = 100;
	unsigned int maxPathDepth = 3;
	unsigned int clampDepth = 0;
	float clampRadiance = 25.0f;
	bool showDebugLightMap = false;
};
