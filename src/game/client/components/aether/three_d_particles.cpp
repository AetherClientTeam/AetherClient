#include "three_d_particles.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
float SmoothLineWidth(float Width)
{
	return std::clamp(Width, 0.42f, 1.45f);
}
}

float CAetherThreeDParticles::Hash01(int Value)
{
	unsigned int X = (unsigned int)Value;
	X ^= X >> 16;
	X *= 0x7feb352du;
	X ^= X >> 15;
	X *= 0x846ca68bu;
	X ^= X >> 16;
	return (X & 0xffff) / 65535.0f;
}

ColorRGBA CAetherThreeDParticles::ConfigColor(int ColorConfig, float Alpha)
{
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(ColorConfig));
	Color.a *= Alpha;
	return Color;
}

void CAetherThreeDParticles::DrawLine(vec2 From, vec2 To, float Width, ColorRGBA Color)
{
	const vec2 Delta = To - From;
	const float Len = length(Delta);
	if(Len <= 0.001f)
		return;
	const vec2 Dir = Delta / Len;
	const vec2 Normal = vec2(Dir.y, -Dir.x) * SmoothLineWidth(Width);
	Graphics()->SetColor(Color);
	IGraphics::CFreeformItem Item(From - Normal, From + Normal, To - Normal, To + Normal);
	Graphics()->QuadsDrawFreeform(&Item, 1);
}

vec2 CAetherThreeDParticles::ProjectPoint(float x, float y, float z, float Size, float RotX, float RotY, float RotZ, vec2 Center) const
{
	x *= Size;
	y *= Size;
	z *= Size;

	const float CX = std::cos(RotX);
	const float SX = std::sin(RotX);
	const float CY = std::cos(RotY);
	const float SY = std::sin(RotY);
	const float CZ = std::cos(RotZ);
	const float SZ = std::sin(RotZ);

	const float Y1 = y * CX - z * SX;
	const float Z1 = y * SX + z * CX;
	const float X2 = x * CY + Z1 * SY;
	const float Z2 = -x * SY + Z1 * CY;
	const float X3 = X2 * CZ - Y1 * SZ;
	const float Y3 = X2 * SZ + Y1 * CZ;

	const float Perspective = std::clamp(1.0f / (1.0f + Z2 * 0.018f), 0.72f, 1.34f);
	return Center + vec2(X3 * Perspective, Y3 * Perspective);
}

void CAetherThreeDParticles::DrawCube(vec2 Center, float Size, float RotX, float RotY, float RotZ, ColorRGBA Color)
{
	constexpr float A = 0.58f;
	const std::array<vec2, 8> Verts = {
		ProjectPoint(-A, -A, -A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(A, -A, -A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(A, A, -A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(-A, A, -A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(-A, -A, A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(A, -A, A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(A, A, A, Size, RotX, RotY, RotZ, Center),
		ProjectPoint(-A, A, A, Size, RotX, RotY, RotZ, Center),
	};

	constexpr int aEdges[12][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7},
	};

	for(int i = 0; i < 12; ++i)
	{
		const bool DepthEdge = i >= 8;
		DrawLine(Verts[aEdges[i][0]], Verts[aEdges[i][1]], Size * (DepthEdge ? 0.026f : 0.032f), Color.WithAlpha(Color.a * (DepthEdge ? 0.78f : 1.0f)));
	}
}

void CAetherThreeDParticles::DrawHeart(vec2 Center, float Size, float RotX, float RotY, float RotZ, ColorRGBA Color)
{
	auto HeartPoint = [](float T) {
		return vec2(
			16.0f * std::pow(std::sin(T), 3.0f),
			-(13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T)));
	};

	constexpr int POINTS = 36;
	std::array<vec2, POINTS + 1> Front;
	std::array<vec2, POINTS + 1> Back;
	for(int i = 0; i <= POINTS; ++i)
	{
		const float T = i / (float)POINTS * 2.0f * pi;
		const vec2 P = HeartPoint(T);
		Front[i] = ProjectPoint(P.x * 0.052f, P.y * 0.052f, -0.12f, Size, RotX, RotY, RotZ, Center);
		Back[i] = ProjectPoint(P.x * 0.052f, P.y * 0.052f, 0.12f, Size, RotX, RotY, RotZ, Center);
	}
	for(int i = 0; i < POINTS; ++i)
	{
		DrawLine(Front[i], Front[i + 1], Size * 0.028f, Color);
		DrawLine(Back[i], Back[i + 1], Size * 0.022f, Color.WithAlpha(Color.a * 0.58f));
	}
	for(int i = 0; i < POINTS; i += 6)
	{
		DrawLine(Front[i], Back[i], Size * 0.020f, Color.WithAlpha(Color.a * 0.72f));
	}
}

void CAetherThreeDParticles::DrawParticle(vec2 Center, float Size, float RotX, float RotY, float RotZ, int Type, ColorRGBA Color)
{
	if(g_Config.m_Ae3DParticlesGlow)
	{
		const ColorRGBA Glow = Color.WithAlpha(Color.a * 0.16f);
		if(Type == 1)
			DrawHeart(Center, Size * 1.18f, RotX, RotY, RotZ, Glow);
		else
			DrawCube(Center, Size * 1.18f, RotX, RotY, RotZ, Glow);
	}
	if(Type == 1)
		DrawHeart(Center, Size, RotX, RotY, RotZ, Color);
	else
		DrawCube(Center, Size, RotX, RotY, RotZ, Color);
}

ColorRGBA CAetherThreeDParticles::ParticleColor(int Seed, int Layer, float Alpha) const
{
	if(!g_Config.m_Ae3DParticlesColorMode)
		return ConfigColor(g_Config.m_Ae3DParticlesColor, Alpha);
	const float Hue = std::fmod(Hash01(Seed * 31 + Layer * 101) * 0.82f + Client()->LocalTime() * 0.015f, 1.0f);
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.72f, 0.72f));
	Color.a = Alpha;
	return Color;
}

void CAetherThreeDParticles::OnRender()
{
	if(!g_Config.m_Ae3DParticles || g_Config.m_Ae3DParticlesCount <= 0)
		return;
	if(g_Config.m_AeFocusMode || (g_Config.m_AeOptimizer && g_Config.m_AeOptimizerDisableParticles))
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float ViewW = std::max(1.0f, ScreenX1 - ScreenX0);
	const float ViewH = std::max(1.0f, ScreenY1 - ScreenY0);
	const vec2 Camera = GameClient()->m_Camera.m_Center;
	const int Total = std::clamp(g_Config.m_Ae3DParticlesCount, 0, 180);
	const float BaseSize = std::clamp((float)g_Config.m_Ae3DParticlesSize, 2.0f, 28.0f);
	const float BaseAlpha = std::clamp(g_Config.m_Ae3DParticlesAlpha / 100.0f, 0.01f, 1.0f);
	const float Speed = std::clamp(g_Config.m_Ae3DParticlesSpeed / 18.0f, 0.0f, 7.0f);
	const float RotationSpeed = std::clamp(g_Config.m_Ae3DParticlesRotationSpeed / 100.0f, 0.0f, 1.8f);

	const std::array<float, 3> aParallax = {0.28f, 0.62f, 1.16f};
	const std::array<float, 3> aLayerSize = {0.72f, 1.0f, 1.28f};
	const std::array<float, 3> aLayerAlpha = {0.42f, 0.72f, 1.0f};
	const std::array<float, 3> aLayerSpeed = {0.34f, 0.72f, 1.22f};
	const std::array<int, 3> aLayerNumerator = {38, 34, 28};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	int DrawnBase = 0;
	for(int Layer = 0; Layer < 3; ++Layer)
	{
		const int LayerCount = Layer == 2 ? Total - DrawnBase : Total * aLayerNumerator[Layer] / 100;
		DrawnBase += LayerCount;
		const float Parallax = aParallax[Layer];
		const vec2 VirtualCenter = Camera * Parallax;
		const float PeriodW = ViewW * 1.45f;
		const float PeriodH = ViewH * 1.45f;
		const vec2 Drift(std::cos(Layer * 1.9f + 0.4f), std::sin(Layer * 2.1f + 1.2f));

		for(int i = 0; i < LayerCount; ++i)
		{
			const int Seed = Layer * 10007 + i * 97;
			vec2 VirtualPos(
				Hash01(Seed + 3) * PeriodW,
				Hash01(Seed + 7) * PeriodH);
			VirtualPos += Drift * (float)Client()->LocalTime() * Speed * aLayerSpeed[Layer] * 18.0f;
			VirtualPos.x += std::floor((VirtualCenter.x - ViewW * 0.65f - VirtualPos.x) / PeriodW) * PeriodW;
			VirtualPos.y += std::floor((VirtualCenter.y - ViewH * 0.65f - VirtualPos.y) / PeriodH) * PeriodH;
			while(VirtualPos.x < VirtualCenter.x + ViewW * 0.65f)
			{
				vec2 Wrapped = VirtualPos;
				while(Wrapped.y < VirtualCenter.y + ViewH * 0.65f)
				{
					const vec2 World = Wrapped + Camera * (1.0f - Parallax);
					if(World.x >= ScreenX0 - 48.0f && World.x <= ScreenX1 + 48.0f && World.y >= ScreenY0 - 48.0f && World.y <= ScreenY1 + 48.0f)
					{
						const float Size = BaseSize * aLayerSize[Layer] * (0.78f + Hash01(Seed + 13) * 0.48f);
						const float Time = (float)Client()->LocalTime() * RotationSpeed;
						const float Dir = Hash01(Seed + 17) > 0.5f ? 1.0f : -1.0f;
						const float RotX = Time * (0.42f + Layer * 0.11f + Hash01(Seed + 29) * 0.20f) * Dir + Hash01(Seed + 31) * 2.0f * pi;
						const float RotY = Time * (0.52f + Layer * 0.13f + Hash01(Seed + 37) * 0.24f) * -Dir + Hash01(Seed + 41) * 2.0f * pi;
						const float RotZ = Hash01(Seed + 47) * 2.0f * pi + std::sin(Time * 0.16f + Seed * 0.01f) * 0.10f;
						int Type = std::clamp(g_Config.m_Ae3DParticlesType, 0, 2);
						if(Type == 2)
							Type = Hash01(Seed + 23) > 0.42f ? 0 : 1;
						DrawParticle(World, Size, RotX, RotY, RotZ, Type, ParticleColor(Seed, Layer, BaseAlpha * aLayerAlpha[Layer]));
					}
					Wrapped.y += PeriodH;
				}
				VirtualPos.x += PeriodW;
			}
		}
	}
	Graphics()->QuadsEnd();
}
