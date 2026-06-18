#ifndef GAME_CLIENT_COMPONENTS_AETHER_THREE_D_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_AETHER_THREE_D_PARTICLES_H

#include <game/client/component.h>

#include <base/color.h>
#include <base/vmath.h>

class CAetherThreeDParticles : public CComponent
{
	static float Hash01(int Value);
	static ColorRGBA ConfigColor(int ColorConfig, float Alpha = 1.0f);

	void DrawLine(vec2 From, vec2 To, float Width, ColorRGBA Color);
	vec2 ProjectPoint(float x, float y, float z, float Size, float RotX, float RotY, float RotZ, vec2 Center) const;
	void DrawCube(vec2 Center, float Size, float RotX, float RotY, float RotZ, ColorRGBA Color);
	void DrawHeart(vec2 Center, float Size, float RotX, float RotY, float RotZ, ColorRGBA Color);
	void DrawParticle(vec2 Center, float Size, float RotX, float RotY, float RotZ, int Type, ColorRGBA Color);
	ColorRGBA ParticleColor(int Seed, int Layer, float Alpha) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
