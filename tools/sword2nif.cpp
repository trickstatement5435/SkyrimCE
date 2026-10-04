// Builds EnergySword.nif for Skyrim SE with nifly:
//   EnergySwordMesh  the sword (glow-mapped: the blade glows, the handle doesn't)
//   EnergySwordHalo  an additive glow shell around the blade
//   box collision (so it can lie on the ground), Prn = WeaponSword (hip sheath node)
// usage: sword2nif sword.bin shell.bin out.nif
#include "NifFile.hpp"
#include "bhk.hpp"
#include "ExtraData.hpp"
#include "Shaders.hpp"
#include <cstdio>
#include <fstream>
using namespace nifly;

struct Mesh
{
	std::vector<Vector3> verts, norms;
	std::vector<Vector2> uvs;
	std::vector<Triangle> tris;
};

static Mesh LoadMesh(const char* path)
{
	Mesh m;
	std::ifstream in(path, std::ios::binary);
	uint32_t nv = 0, nt = 0;
	in.read((char*)&nv, 4);
	in.read((char*)&nt, 4);
	m.verts.resize(nv); m.norms.resize(nv); m.uvs.resize(nv); m.tris.resize(nt);
	for (uint32_t i = 0; i < nv; ++i) {
		float d[8];
		in.read((char*)d, sizeof d);
		m.verts[i] = Vector3(d[0], d[1], d[2]);
		m.norms[i] = Vector3(d[3], d[4], d[5]);
		m.uvs[i] = Vector2(d[6], d[7]);
	}
	for (uint32_t i = 0; i < nt; ++i) {
		uint16_t t[3];
		in.read((char*)t, sizeof t);
		m.tris[i] = Triangle(t[0], t[1], t[2]);
	}
	return m;
}

static void AddSword(NifFile& nif, const Mesh& m)
{
	NiShape* shape = nif.CreateShapeFromData("EnergySwordMesh", &m.verts, &m.tris, &m.uvs, &m.norms);
	nif.CalcTangentsForShape(shape);
	std::string d = "textures\\EnergySword\\energysword.dds", n = "textures\\EnergySword\\energysword_n.dds", g = "textures\\EnergySword\\energysword_g.dds";
	nif.SetTextureSlot(shape, d, 0);
	nif.SetTextureSlot(shape, n, 1);
	nif.SetTextureSlot(shape, g, 2);
	if (auto sh = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(shape))) {
		sh->SetShaderType(BSLSP_GLOWMAP);
		sh->shaderFlags1 |= SLSF1_SPECULAR | SLSF1_OWN_EMIT;
		sh->shaderFlags2 |= SLSF2_GLOW_MAP | SLSF2_DOUBLE_SIDED;
		sh->specularStrength = 1.5f;
		sh->glossiness = 60.0f;
		sh->emissiveColor = Vector3(0.55f, 0.85f, 1.0f);
		sh->emissiveMultiple = 3.0f;
	}
}

static void AddHalo(NifFile& nif, const Mesh& m)
{
	NiShape* shape = nif.CreateShapeFromData("EnergySwordHalo", &m.verts, &m.tris, &m.uvs, &m.norms);
	auto& hdr = nif.GetHeader();
	// swap the default lighting shader for an unlit additive effect shader
	auto fx = std::make_unique<BSEffectShaderProperty>();
	fx->shaderFlags1 = SLSF1_ZBUFFER_TEST | SLSF1_USE_FALLOFF;
	fx->shaderFlags2 = SLSF2_DOUBLE_SIDED;
	fx->sourceTexture.get() = "textures\\EnergySword\\energysword_halo.dds";
	fx->baseColor = Color4(0.45f, 0.8f, 1.0f, 1.0f);
	fx->baseColorScale = 1.6f;
	fx->falloffStartAngle = 1.0f;
	fx->falloffStopAngle = 0.0f;
	fx->falloffStartOpacity = 0.9f;
	fx->falloffStopOpacity = 0.0f;
	fx->textureClampMode = 3;
	const uint32_t old = shape->ShaderPropertyRef()->index;
	hdr.ReplaceBlock(old, std::move(fx));
	auto alpha = std::make_unique<NiAlphaProperty>();
	alpha->flags = 0x0D | (0 << 5);  // blend on, src = src alpha, dest = one (additive)
	alpha->threshold = 0;
	nif.AssignAlphaProperty(shape, std::move(alpha));
}

static void AddCollision(NifFile& nif, const Mesh& m)
{
	auto& hdr = nif.GetHeader();
	auto root = nif.GetRootNode();
	auto bsx = std::make_unique<BSXFlags>();
	bsx->name.get() = "BSX";
	bsx->integerData = 2;
	nif.AssignExtraData(root, std::move(bsx));

	Vector3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
	for (auto& p : m.verts) {
		mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
		mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
	}
	const float s = 0.0142875f;
	mn = mn * s;
	mx = mx * s;
	auto shape = std::make_unique<bhkConvexVerticesShape>();
	shape->SetMaterial(1288358971u);  // SKY_HAV_MAT_SOLID_METAL
	shape->radius = 0.01f;
	for (int i = 0; i < 8; ++i) { Vector4 tmp((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 0.0f); shape->verts.push_back(tmp); }
	{ Vector4 tmp(1, 0, 0, -mx.x); shape->normals.push_back(tmp); }
	{ Vector4 tmp(-1, 0, 0, mn.x); shape->normals.push_back(tmp); }
	{ Vector4 tmp(0, 1, 0, -mx.y); shape->normals.push_back(tmp); }
	{ Vector4 tmp(0, -1, 0, mn.y); shape->normals.push_back(tmp); }
	{ Vector4 tmp(0, 0, 1, -mx.z); shape->normals.push_back(tmp); }
	{ Vector4 tmp(0, 0, -1, mn.z); shape->normals.push_back(tmp); }
	const uint32_t shapeId = hdr.AddBlock(std::move(shape));

	auto body = std::make_unique<bhkRigidBody>();
	body->shapeRef.index = shapeId;
	body->collisionFilter.layer = 5;  // OL_WEAPON
	body->collisionFilterCopy = body->collisionFilter;
	body->broadPhaseType = 1;
	body->rotation = QuaternionXYZW{ 0, 0, 0, 1 };
	body->center = Vector4((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2, 0);
	body->friction = 0.5f;
	body->restitution = 0.2f;
	body->motionSystem = 6;  // keyframed: follows the hand / sits where placed (same as the Gravity Gun)
	body->deactivatorType = 1;
	body->solverDeactivation = 1;
	body->qualityType = 2;
	body->mass = 0.0f;
	const uint32_t bodyId = hdr.AddBlock(std::move(body));

	auto col = std::make_unique<bhkCollisionObject>();
	col->flags = 0x1;
	col->targetRef.index = nif.GetBlockID(root);
	col->bodyRef.index = bodyId;
	root->collisionRef.index = hdr.AddBlock(std::move(col));

	auto prn = std::make_unique<NiStringExtraData>();
	prn->name.get() = "Prn";
	prn->stringData.get() = "WeaponSword";
	nif.AssignExtraData(root, std::move(prn));
}

int main(int argc, char** argv)
{
	if (argc < 4) {
		std::printf("usage: sword2nif sword.bin shell.bin out.nif\n");
		return 1;
	}
	const Mesh sword = LoadMesh(argv[1]);
	const Mesh shell = LoadMesh(argv[2]);
	NifFile nif;
	nif.Create(NiVersion::getSSE());
	auto fade = std::make_unique<BSFadeNode>();
	fade->name.get() = "EnergySword";
	nif.GetHeader().ReplaceBlock(0, std::move(fade));
	AddSword(nif, sword);
	AddHalo(nif, shell);
	AddCollision(nif, sword);
	nif.PrettySortBlocks();
	if (nif.Save(argv[3]) != 0) return 1;
	NifFile check;
	if (check.Load(argv[3]) != 0) return 1;
	std::printf("%s: %zu shapes, %u blocks\n", argv[3], check.GetShapes().size(), check.GetHeader().GetNumBlocks());
	for (auto s : check.GetShapes()) {
		auto sh = check.GetShader(s);
		std::printf("  %s shader %s alpha %d\n", s->name.get().c_str(), sh ? sh->GetBlockName() : "-", check.GetAlphaProperty(s) ? 1 : 0);
	}
	return 0;
}
