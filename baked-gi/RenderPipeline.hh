#pragma once

#include "Mesh.hh"
#include "DirectionalLight.hh"

#include <glm/ext.hpp>
#include <glow/fwd.hh>
#include <glow-extras/camera/GenericCamera.hh>
#include <vector>

enum class DebugImageLocation {
	TopRight,
	BottomRight
};

class RenderPipeline {
public:
	RenderPipeline();

	void render(const std::vector<Mesh>& meshes);
	void resizeBuffers(int w, int h);

	void setAmbientColor(const glm::vec3& color);
	void attachCamera(const glow::camera::GenericCamera& camera);
	void attachLight(const DirectionalLight& light);

	void setDebugTexture(const glow::SharedTexture2D& texture, DebugImageLocation location);

private:
	glow::SharedTextureRectangle hdrColorBuffer;
	glow::SharedTextureRectangle brightnessBuffer;
	glow::SharedTextureRectangle depthBuffer;
	glow::SharedFramebuffer hdrFbo;

	glow::SharedTextureRectangle blurColorBufferA;
	glow::SharedTextureRectangle blurColorBufferB;
	glow::SharedFramebuffer blurFboA;
	glow::SharedFramebuffer blurFboB;

	glow::SharedProgram objectShader;
	glow::SharedProgram objectNoTexShader;
	glow::SharedProgram skyboxShader;
	glow::SharedProgram downsampleShader;
	glow::SharedProgram blurShader;
	glow::SharedProgram postProcessShader;
	glow::SharedProgram debugImageShader;

	glow::SharedVertexArray vaoQuad;
	glow::SharedVertexArray vaoCube;

	glow::SharedTexture2D topRightDebugTexture;
	glow::SharedTexture2D bottomRightDebugTexture;
	glow::SharedTextureCubeMap skybox;

	const glow::camera::GenericCamera* camera;
	glm::vec3 ambientColor = glm::vec3(0.0f);
	const DirectionalLight* light;
};