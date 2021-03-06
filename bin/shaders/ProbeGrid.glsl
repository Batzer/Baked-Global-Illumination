uniform vec3 uProbeGridDimensions;
uniform vec3 uProbeGridCellSize;
uniform vec3 uProbeGridMin;
uniform vec3 uProbeGridMax;
uniform sampler1DArray uProbeInfluenceTexture; // 0 = pos, 0.5 = min, 1 = max
uniform sampler3D uProbeVisibilityTexture; // The three probe layers used for the i-th voxel. Dim = w * h * d

vec3 getProbeGridCell(vec3 worldPos) {
	return floor((worldPos - uProbeGridMin) / uProbeGridCellSize);
}

float getProbeLayer(vec3 coord) {
	return coord.x + coord.y * uProbeGridDimensions.x + coord.z * uProbeGridDimensions.x * uProbeGridDimensions.y;
}

vec3 getProbePosition(float layer) {
	return texelFetch(uProbeInfluenceTexture, ivec2(0, int(layer)), 0).xyz;
}

vec3 getProbeInfluenceBoxMin(float layer) {
	return texelFetch(uProbeInfluenceTexture, ivec2(1, int(layer)), 0).xyz;
}

vec3 getProbeInfluenceBoxMax(float layer) {
	return texelFetch(uProbeInfluenceTexture, ivec2(2, int(layer)), 0).xyz;
}

vec3 getProbeLayersForVoxel(vec3 coord) {
	return texelFetch(uProbeVisibilityTexture, ivec3(coord), 0).xyz;
}
