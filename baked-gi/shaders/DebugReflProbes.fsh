uniform samplerCubeArray uEnvMapArray;
uniform float uMipLevel;
uniform uint uLayer;

in vec3 vTexCoord;

out vec3 fColor;
out vec3 fBrightColor;

void main() {
    fColor = textureLod(uEnvMapArray, vec4(vTexCoord, float(uLayer)), uMipLevel).rgb;
    fBrightColor = vec3(0.0);
}
