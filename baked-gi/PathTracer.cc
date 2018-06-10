#include "PathTracer.hh"

#include <glow/objects/Texture2D.hh>
#include <glow/data/SurfaceData.hh>

#include <xmmintrin.h>
#include <limits>
#include <cmath>
#include <algorithm>
#include <random>

#if !defined(_MM_SET_DENORMALS_ZERO_MODE)
#define _MM_DENORMALS_ZERO_ON   (0x0040)
#define _MM_DENORMALS_ZERO_OFF  (0x0000)
#define _MM_DENORMALS_ZERO_MASK (0x0040)
#define _MM_SET_DENORMALS_ZERO_MODE(x) (_mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | (x)))
#endif

namespace {
	struct Ray : public RTCRay {
		Ray(const glm::vec3& origin, const glm::vec3& dir, float tnear, float tfar) {
			this->org_x = origin.x;
			this->org_y = origin.y;
			this->org_z = origin.z;
			this->dir_x = dir.x;
			this->dir_y = dir.y;
			this->dir_z = dir.z;
			this->tnear = tnear;
			this->tfar = tfar;
			this->time = 0.0f;
		}
	};

	struct Hit : public RTCHit {
		Hit() {
			geomID = RTC_INVALID_GEOMETRY_ID;
		}
	};


	std::random_device randDevice;
	std::default_random_engine randEngine(randDevice());
	std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

	void makeCoordinateSystem(const glm::vec3& normal, glm::vec3& xAxis, glm::vec3& yAxis) {
		xAxis = glm::vec3(1.0f, 0.0f, 0.0f);
		if (std::abs(1.0f - normal.x) < 1.0e-8f) {
			xAxis = glm::vec3(0.0f, 0.0f, -1.0f);
		}
		else if (std::abs(1.0f + normal.x) < 1.0e-8f) {
			xAxis = glm::vec3(0.0f, 0.0f, 1.0f);
		}

		yAxis = glm::normalize(glm::cross(normal, xAxis));
		xAxis = glm::normalize(glm::cross(yAxis, normal));
	}

	glm::vec3 sampleCosineHemisphere(const glm::vec3& normal) {
		float u1 = uniformDist(randEngine);
		float u2 = uniformDist(randEngine);
		
		float r = std::sqrt(u1);
		float phi = 2.0f * glm::pi<float>() * u2;

		float x = r * std::sin(phi);
		float y = r * std::cos(phi);
		float z = std::sqrt(1.0f - x * x - y * y);

		glm::vec3 xAxis;
		glm::vec3 yAxis;
		makeCoordinateSystem(normal, xAxis, yAxis);

		return glm::normalize(x * xAxis + y * yAxis + z * normal);
	}

	float pdfCosineHemisphere(const glm::vec3& normal, const glm::vec3& wi) {
		return glm::dot(normal, wi) / glm::pi<float>();
	}

	glm::vec3 brdfLambert(const glm::vec3& diffuse) {
		return diffuse / glm::pi<float>();
	}

	glm::vec3 sampleGGX(const glm::vec3& normal, float roughness) {
		float u1 = uniformDist(randEngine);
		float u2 = uniformDist(randEngine);

		float alpha = roughness * roughness;
		float phi = 2.0f * glm::pi<float>() * u1;
		float theta = std::acos(std::sqrt((1.0f - u2) / ((alpha * alpha - 1.0f) * u2 + 1.0f)));

		float x = std::sin(theta) * std::cos(phi);
		float y = std::sin(theta) * std::sin(phi);
		float z = std::cos(theta);

		glm::vec3 xAxis;
		glm::vec3 yAxis;
		makeCoordinateSystem(normal, xAxis, yAxis);

		return glm::normalize(x * xAxis + y * yAxis + z * normal);
	}

	float pdfGGX(const glm::vec3& normal, const glm::vec3& wi, float roughness) {
		float alpha = roughness * roughness;
		float alphaSq = alpha * alpha;
		float cosTheta = std::max(0.0f, glm::dot(normal, wi));
		float denom = (alphaSq - 1.0f) * cosTheta * cosTheta + 1.0f;
		return alphaSq * cosTheta / (glm::pi<float>() * denom * denom);
	}

	glm::vec3 brdfCookTorrenceGGX(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L, float roughness, const glm::vec3& F0) {
		glm::vec3 H = glm::normalize(V + L);

		float dotNH = std::max(0.0f, glm::dot(N, H));
		float dotNV = std::max(0.0f, glm::dot(N, V));
		float dotNL = std::max(0.0f, glm::dot(N, L));
		float dotVH = std::max(0.0f, glm::dot(V, H));

		float alpha = roughness * roughness;
		float alphaSq = alpha * alpha;
		float denom = dotNH * dotNH * (alphaSq - 1.0f) + 1.0f;
		float D = alphaSq / (glm::pi<float>() * denom * denom);

		float k = alpha / 2.0f;
		float G_l = dotNL / (dotNL * (1.0f - k) + k);
		float G_v = dotNV / (dotNV * (1.0f - k) + k);
		float G = G_l * G_v;

		glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(1.0f - dotVH, 5.0f);

		return D * G * F / std::max(4.0f * dotNL * dotNV, 1e-8f);
	}

	glm::vec3 gammaToLinear(const glm::vec3& v) {
		return { std::pow(v.x, 2.2f), std::pow(v.y, 2.2f) , std::pow(v.z, 2.2f) };
	}

	glm::vec3 linearToGamma(const glm::vec3& v) {
		return { std::pow(v.x, 1.0f / 2.2f), std::pow(v.y, 1.0f / 2.2f) , std::pow(v.z, 1.0f / 2.2f) };
	}
}

PathTracer::PathTracer() {
	_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
	_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

	device = rtcNewDevice("verbose=1");
}

PathTracer::~PathTracer() {
	if (scene) {
		rtcReleaseScene(scene);
	}
	if (device) {
		rtcReleaseDevice(device);
	}
}

void PathTracer::buildScene(const std::vector<Primitive>& primitives) {
	if (scene) {
		rtcReleaseScene(scene);
	}
	scene = rtcNewScene(device);
	rtcSetSceneFlags(scene, RTCSceneFlags::RTC_SCENE_FLAG_ROBUST);
	rtcSetSceneBuildQuality(scene, RTCBuildQuality::RTC_BUILD_QUALITY_HIGH);
	
	for (const auto& primitive : primitives) {
		RTCGeometry mesh = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
		rtcSetGeometryBuildQuality(mesh, RTCBuildQuality::RTC_BUILD_QUALITY_HIGH);
		rtcSetGeometryVertexAttributeCount(mesh, 3);
		
		auto indexBuffer = rtcSetNewGeometryBuffer(mesh, RTC_BUFFER_TYPE_INDEX,
			0, RTC_FORMAT_UINT3, sizeof(unsigned int) * 3, primitive.indices.size());
		std::memcpy(indexBuffer, primitive.indices.data(), primitive.indices.size() * sizeof(unsigned int));

		auto vertexBuffer = static_cast<glm::vec3*>(rtcSetNewGeometryBuffer(mesh, RTC_BUFFER_TYPE_VERTEX,
			0, RTC_FORMAT_FLOAT3, sizeof(glm::vec3), primitive.positions.size()));
		std::memcpy(vertexBuffer, primitive.positions.data(), primitive.positions.size() * sizeof(glm::vec3));

		// Bake the transform into the positions
		for (std::size_t i = 0; i < primitive.positions.size(); ++i) {
			vertexBuffer[i] = glm::vec3(primitive.transform * glm::vec4(vertexBuffer[i], 1.0f));
		}

		auto normalBuffer = static_cast<glm::vec3*>(rtcSetNewGeometryBuffer(mesh, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
			0, RTC_FORMAT_FLOAT3, sizeof(glm::vec3), primitive.normals.size()));
		std::memcpy(normalBuffer, primitive.normals.data(), primitive.normals.size() * sizeof(glm::vec3));

		// Bake the transform into the normals
		auto normalMatrix = glm::mat3(glm::transpose(glm::inverse(primitive.transform)));
		for (std::size_t i = 0; i < primitive.normals.size(); ++i) {
			normalBuffer[i] = normalMatrix * normalBuffer[i];
		}

		if (!primitive.tangents.empty()) {
			auto tangentBuffer = static_cast<glm::vec4*>(rtcSetNewGeometryBuffer(mesh, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
				1, RTC_FORMAT_FLOAT4, sizeof(glm::vec4), primitive.tangents.size()));
			std::memcpy(tangentBuffer, primitive.tangents.data(), primitive.tangents.size() * sizeof(glm::vec4));

			// Bake the transform into the tangents
			for (std::size_t i = 0; i < primitive.tangents.size(); ++i) {
				tangentBuffer[i] = glm::vec4(normalMatrix * (glm::vec3(tangentBuffer[i]) * tangentBuffer[i].w), 1.0f);
			}
		}

		if (!primitive.texCoords.empty()) {
			auto texCoordBuffer = static_cast<glm::vec2*>(rtcSetNewGeometryBuffer(mesh, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
				2, RTC_FORMAT_FLOAT2, sizeof(glm::vec2), primitive.texCoords.size()));
			std::memcpy(texCoordBuffer, primitive.texCoords.data(), primitive.texCoords.size() * sizeof(glm::vec2));
		}

		rtcCommitGeometry(mesh);
		auto geomID = rtcAttachGeometry(scene, mesh);
		rtcReleaseGeometry(mesh);

		Material material;
		material.albedoMap = primitive.albedoMap;
		material.normalMap = primitive.normalMap;
		material.roughnessMap = primitive.roughnessMap;
		material.baseColor = primitive.baseColor;
		material.roughness = primitive.roughness;
		material.metallic = primitive.metallic;
		materials.insert({ geomID, material });
	}

	rtcCommitScene(scene);
}

void PathTracer::setLight(const DirectionalLight& light) {
	this->light = &light;
}

void PathTracer::traceDebugImage() {
	std::vector<glm::vec3> colors(debugImage.size(), glm::vec3(0.0f));

	const int numSamples = 200;
	for (int k = 0; k < numSamples; ++k) {

		#pragma omp parallel for
		for (int y = 0; y < debugImageHeight; ++y) {
			for (int x = 0; x < debugImageWidth; ++x) {
				float aspect = debugImageWidth / static_cast<float>(debugImageHeight);
				float fov = debugCamera->getHorizontalFieldOfView();
				float scale = std::tan(glm::radians(fov * 0.5f));
				float newX = x + uniformDist(randEngine) - 0.5f;
				float newY = y + uniformDist(randEngine) - 0.5f;
				float px = (2.0f * ((newX + 0.5f) / debugImageWidth) - 1.0f) * scale * aspect;
				float py = (1.0f - 2.0f * ((newY + 0.5f) / debugImageHeight)) * scale;

				glm::vec3 dir = glm::transpose(debugCamera->getViewMatrix()) * glm::vec4(px, py, -1, 0);
				colors[x + y * debugImageWidth] += trace(debugCamera->getPosition(), dir, glm::vec3(1.0f));
			}
		}
	}

	for (std::size_t i = 0; i < colors.size(); ++i) {
		debugImage[i] = colors[i] / static_cast<float>(numSamples);
		debugImage[i] = debugImage[i] / (debugImage[i] + glm::vec3(1.0f)); // Tone mapping
		debugImage[i] = linearToGamma(debugImage[i]); // Gamma correction
	}

	debugTexture->bind().setData(GL_RGB, debugImageWidth, debugImageHeight, debugImage);
}

void PathTracer::attachDebugCamera(const glow::camera::GenericCamera& camera) {
	debugCamera = &camera;
}

void PathTracer::setDebugImageSize(int width, int height) {
	debugImageWidth = width;
	debugImageHeight = height;
	debugImage.resize(width * height);
	if (!debugTexture) {
		debugTexture = glow::Texture2D::create(width, height, GL_RGB);
		debugTexture->bind().setFilter(GL_LINEAR, GL_LINEAR);
	}
	else {
		debugTexture->bind().resize(width, height);
	}
}

glow::SharedTexture2D PathTracer::getDebugTexture() const {
	return debugTexture;
}

glm::vec3 PathTracer::trace(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& weight, int depth) {
	const int maxDepth = 5;
	if (depth > maxDepth) {
		return weight * glm::vec3{ 0.0f, 0.0f, 0.0f };
	}
	
	RTCRayHit rayhit = { Ray(origin, dir, 0.001f, std::numeric_limits<float>::infinity()), Hit() };
	RTCIntersectContext context;
	rtcInitIntersectContext(&context);
	rtcIntersect1(scene, &context, &rayhit);

	if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
		return { 0.0f, 0.0f, 0.0f };
	}

	glm::vec3 surfacePoint;
	surfacePoint.x = rayhit.ray.org_x + rayhit.ray.dir_x * rayhit.ray.tfar;
	surfacePoint.y = rayhit.ray.org_y + rayhit.ray.dir_y * rayhit.ray.tfar;
	surfacePoint.z = rayhit.ray.org_z + rayhit.ray.dir_z * rayhit.ray.tfar;
	
	alignas(16) glm::vec3 normal;
	rtcInterpolate0(rtcGetGeometry(scene, rayhit.hit.geomID), rayhit.hit.primID,
		rayhit.hit.u, rayhit.hit.v, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &normal[0], 3);
	normal = glm::normalize(normal);
	
	auto material = materials[rayhit.hit.geomID];
	glm::vec3 albedo;
	float roughness = material.roughness;
	if (material.albedoMap) {
		alignas(16) glm::vec2 texCoord;
		rtcInterpolate0(rtcGetGeometry(scene, rayhit.hit.geomID), rayhit.hit.primID,
			rayhit.hit.u, rayhit.hit.v, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 2, &texCoord[0], 2);
		albedo = gammaToLinear(glm::vec3(material.albedoMap->sample(texCoord))) * gammaToLinear(material.baseColor);

		roughness *= material.roughnessMap->sample(texCoord).x;

		alignas(16) glm::vec4 tangent;
		rtcInterpolate0(rtcGetGeometry(scene, rayhit.hit.geomID), rayhit.hit.primID,
			rayhit.hit.u, rayhit.hit.v, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 1, &tangent[0], 4);

		glm::vec3 normalMapN = glm::vec3(material.normalMap->sample({ texCoord[0], texCoord[1] }));
		normalMapN.x = normalMapN.x * 2.0f - 1.0f;
		normalMapN.y = normalMapN.y * 2.0f - 1.0f;

		glm::vec3 N = glm::normalize(normal);
		glm::vec3 T = glm::normalize(glm::vec3(tangent)) * tangent.w;
		glm::vec3 B = glm::normalize(glm::cross(N, glm::vec3(T)));
		glm::mat3 TBN = glm::mat3(T, B, N);
		normal = glm::normalize(TBN * normalMapN);
	}
	else {
		albedo = gammaToLinear(material.baseColor);
	}

	Ray occluderRay(surfacePoint + normal * 0.001f, glm::normalize(-light->direction), 0.0f, std::numeric_limits<float>::infinity());
	RTCIntersectContext occluderContext;
	rtcInitIntersectContext(&occluderContext);
	rtcOccluded1(scene, &occluderContext, &occluderRay);

	glm::vec3 diffuse = albedo * (1 - material.metallic);
	glm::vec3 specular = glm::mix(glm::vec3(0.04f), albedo, material.metallic);

	glm::vec3 directIllumination(0.0f);
	if (occluderRay.tfar >= 0.0f) {
		glm::vec3 E = glm::vec3(rayhit.ray.org_x, rayhit.ray.org_y, rayhit.ray.org_z);
		glm::vec3 L = glm::normalize(-light->direction);
		glm::vec3 V = glm::normalize(E - surfacePoint);
		
		glm::vec3 shadingDiffuse = brdfLambert(diffuse);
		glm::vec3 shadingSpecular = brdfCookTorrenceGGX(normal, V, L, std::max(0.01f, roughness), specular);
		glm::vec3 shading = shadingDiffuse + shadingSpecular;

		directIllumination = weight * shading * std::max(glm::dot(normal, L), 0.0f) * gammaToLinear(light->color) * light->power;
	}

	float Pr = std::max(diffuse.x + specular.x, std::max(diffuse.y + specular.y, diffuse.z + specular.z));
	float Pd = Pr * (diffuse.x + diffuse.y + diffuse.z) / (diffuse.x + diffuse.y + diffuse.z + specular.x + specular.y + specular.z);
	float Ps = Pr * (specular.x + specular.y + specular.z) / (diffuse.x + diffuse.y + diffuse.z + specular.x + specular.y + specular.z);
	float randVar = uniformDist(randEngine);

	glm::vec3 indirectIllumination(0.0f);
	if (randVar < Pd) {
		glm::vec3 brdf = brdfLambert(diffuse);

		glm::vec3 wi = sampleCosineHemisphere(normal);
		float pdf = pdfCosineHemisphere(normal, wi);

		glm::vec3 newWeight = weight * std::max(glm::dot(normal, wi), 0.0f) * brdf / (pdf * Pd);
		indirectIllumination += newWeight * trace(surfacePoint, wi, newWeight, depth + 1);
	}
	else if (randVar < Pd + Ps) {
		glm::vec3 E = glm::vec3(rayhit.ray.org_x, rayhit.ray.org_y, rayhit.ray.org_z);
		glm::vec3 L = glm::normalize(-light->direction);
		glm::vec3 V = glm::normalize(E - surfacePoint);
		glm::vec3 brdf = brdfCookTorrenceGGX(normal, V, L, glm::max(0.01f, roughness), specular);

		glm::vec3 wi = sampleGGX(normal, glm::max(0.01f, roughness));
		float pdf = pdfGGX(normal, wi, glm::max(0.01f, roughness));

		glm::vec3 newWeight = weight * std::max(glm::dot(normal, wi), 0.0f) * brdf / (pdf * Ps);
		indirectIllumination += newWeight * trace(surfacePoint, wi, newWeight, depth + 1);
	}
	else {
		// Absorb
	}

	return glm::clamp(directIllumination + indirectIllumination, 0.0f, 15.0f);
}