/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "material.h"
#include "material_copy.hpp"
#include "materialmanager.h"
#include <sharedutils/alpha_mode.hpp>
#include <sharedutils/util_shaderinfo.hpp>
#include <sstream>
#include <fsys/filesystem.h>
#include <datasystem_vector.h>
#include <sharedutils/util_string.h>
#include <sharedutils/util_file.h>
#include <sharedutils/util_path.hpp>
#include <datasystem_color.h>
#include <udm.hpp>

DEFINE_BASE_HANDLE(DLLMATSYS,Material,Material);

#undef CreateFile

decltype(Material::DIFFUSE_MAP_IDENTIFIER) Material::DIFFUSE_MAP_IDENTIFIER = "diffuse_map";
decltype(Material::ALBEDO_MAP_IDENTIFIER) Material::ALBEDO_MAP_IDENTIFIER = "albedo_map";
decltype(Material::ALBEDO_MAP2_IDENTIFIER) Material::ALBEDO_MAP2_IDENTIFIER = "albedo_map2";
decltype(Material::ALBEDO_MAP3_IDENTIFIER) Material::ALBEDO_MAP3_IDENTIFIER = "albedo_map3";
decltype(Material::NORMAL_MAP_IDENTIFIER) Material::NORMAL_MAP_IDENTIFIER = "normal_map";
decltype(Material::GLOW_MAP_IDENTIFIER) Material::GLOW_MAP_IDENTIFIER = "emission_map";
decltype(Material::EMISSION_MAP_IDENTIFIER) Material::EMISSION_MAP_IDENTIFIER = GLOW_MAP_IDENTIFIER;
decltype(Material::PARALLAX_MAP_IDENTIFIER) Material::PARALLAX_MAP_IDENTIFIER = "parallax_map";
decltype(Material::ALPHA_MAP_IDENTIFIER) Material::ALPHA_MAP_IDENTIFIER = "alpha_map";
decltype(Material::RMA_MAP_IDENTIFIER) Material::RMA_MAP_IDENTIFIER = "rma_map";
decltype(Material::DUDV_MAP_IDENTIFIER) Material::DUDV_MAP_IDENTIFIER = "dudv_map";
decltype(Material::WRINKLE_STRETCH_MAP_IDENTIFIER) Material::WRINKLE_STRETCH_MAP_IDENTIFIER = "wrinkle_stretch_map";
decltype(Material::WRINKLE_COMPRESS_MAP_IDENTIFIER) Material::WRINKLE_COMPRESS_MAP_IDENTIFIER = "wrinkle_compress_map";
decltype(Material::EXPONENT_MAP_IDENTIFIER) Material::EXPONENT_MAP_IDENTIFIER = "exponent_map";

Material::Material(MaterialManager &manager)
	: m_handle(new PtrMaterial(this)),m_data(nullptr),m_shader(nullptr),m_manager(manager)
{
	Reset();
}

Material::Material(MaterialManager &manager,const util::WeakHandle<util::ShaderInfo> &shaderInfo,const std::shared_ptr<ds::Block> &data)
	: Material(manager)
{
	Initialize(shaderInfo,data);
}

Material::Material(MaterialManager &manager,const std::string &shader,const std::shared_ptr<ds::Block> &data)
	: Material(manager)
{
	Initialize(shader,data);
}
MaterialHandle Material::GetHandle() {return m_handle;}

void Material::Remove() {delete this;}

void Material::Reset()
{
	umath::set_flag(m_stateFlags,StateFlags::Loaded,false);
	m_data = nullptr;
	m_shaderInfo.reset();
	m_shader = nullptr;
	m_texDiffuse = nullptr;
	m_texNormal = nullptr;
	m_userData = nullptr;
	m_texGlow = nullptr;
	m_texParallax = nullptr;
	m_texRma = nullptr;
	m_texAlpha = nullptr;
}

void Material::Initialize(const util::WeakHandle<util::ShaderInfo> &shaderInfo,const std::shared_ptr<ds::Block> &data)
{
	Reset();
	SetShaderInfo(shaderInfo);
	m_data = data;
	UpdateTextures();
}

void Material::Initialize(const std::string &shader,const std::shared_ptr<ds::Block> &data)
{
	Reset();
	m_shader = std::make_unique<std::string>(shader);
	m_data = data;
	UpdateTextures();
}

void *Material::GetUserData() {return m_userData;}
void Material::SetUserData(void *data) {m_userData = data;}

bool Material::IsTranslucent() const {return m_alphaMode == AlphaMode::Blend;}

static bool read_image_size(std::string &imgFile,uint32_t &width,uint32_t &height)
{
	TextureType type;
	imgFile = translate_image_path(imgFile,type);
	auto r = uimg::read_image_size(imgFile,width,height);
	auto rootPath = MaterialManager::GetRootMaterialLocation() +"/";
	imgFile = imgFile.substr(rootPath.length());
	return r;
}

static TextureInfo get_texture_info(const std::string &value)
{
	auto str = value;
	if(str.empty())
	{
		m_value.texture = nullptr;
		m_value.width = 0;
		m_value.height = 0;
		return;
	}
	uint32_t width;
	uint32_t height;
	if(read_image_size(str,width,height) == true)
	{
		m_value.texture = NULL;
		m_value.width = width;
		m_value.height = height;
	}
	else
	{
		m_value.texture = NULL;
		m_value.width = 0;
		m_value.height = 0;
	}
	m_value.name = str;
}
void Material::UpdateTextures()
{
	auto texInfos = std::move(m_texInfos);
	m_texInfos.clear();
	auto &texData = GetTextureData();
	for(auto it=texData.begin<udm::String>();it!=texData.end<udm::String>();++it)
	{
		auto &texName = *it;
		auto itInfo = texInfos.find(texName);
		if(itInfo != texInfos.end())
		{
			m_texInfos[texName] = std::move(itInfo->second);
			continue;
		}
		m_texInfos[texName] = get_texture_info(texName);
	}
	m_texDiffuse = GetTextureInfo(DIFFUSE_MAP_IDENTIFIER);
	if(!m_texDiffuse)
		m_texDiffuse = GetTextureInfo(ALBEDO_MAP_IDENTIFIER);

	m_texNormal = GetTextureInfo(NORMAL_MAP_IDENTIFIER);
	m_texGlow = GetTextureInfo(EMISSION_MAP_IDENTIFIER);
	m_texParallax = GetTextureInfo(PARALLAX_MAP_IDENTIFIER);
	m_texAlpha = GetTextureInfo(ALPHA_MAP_IDENTIFIER);
	m_texRma = GetTextureInfo(RMA_MAP_IDENTIFIER);

	auto &data = GetPropertyData();
	data["alpha_mode"](m_alphaMode);
}

void Material::SetShaderInfo(const util::WeakHandle<util::ShaderInfo> &shaderInfo)
{
	m_shaderInfo = shaderInfo;
	m_shader = nullptr;
}

Material::~Material()
{
	m_handle.Invalidate();
	for(auto &hCb : m_callOnLoaded)
	{
		if(hCb.IsValid() == true)
			hCb.Remove();
	}
}

Material *Material::Copy() const {return Copy<Material>();}

bool Material::IsValid() const {return (m_data != nullptr) ? true : false;}
MaterialManager &Material::GetManager() const {return m_manager;}
void Material::SetLoaded(bool b)
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::ExecutingOnLoadCallbacks))
		return; // Prevent possible recursion while on-load callbacks are being executed
	umath::set_flag(m_stateFlags,StateFlags::Loaded,b);
	if(b == true)
	{
		umath::set_flag(m_stateFlags,StateFlags::ExecutingOnLoadCallbacks,true);
		for(auto &f : m_callOnLoaded)
		{
			if(f.IsValid())
				f();
		}
		m_callOnLoaded.clear();
		umath::set_flag(m_stateFlags,StateFlags::ExecutingOnLoadCallbacks,false);
	}
}
bool Material::Save(udm::AssetData outData,std::string &outErr)
{
	auto udm = (*outData)[GetShaderIdentifier()];
	outData.SetAssetType(PMAT_IDENTIFIER);
	outData.SetAssetVersion(PMAT_VERSION);
	udm["properties"] = GetData()["properties"];
	udm["textures"] = GetData()["textures"];
	return true;
}
extern const std::array<std::string,5> g_knownMaterialFormats;
bool Material::Save(const std::string &relFileName,std::string &outErr,bool absolutePath)
{
	auto udmData = udm::Data::Create();
	std::string err;
	auto result = Save(udmData->GetAssetData(),err);
	if(result == false)
		return false;
	auto fileName = relFileName;
	if(absolutePath == false)
		fileName = "materials/" +fileName;
	FileManager::CreatePath(ufile::get_path_from_filename(fileName).c_str());
	auto writeFileName = fileName;
	ufile::remove_extension_from_filename(writeFileName,g_knownMaterialFormats);
	writeFileName += '.' +std::string{FORMAT_MATERIAL_ASCII};
	auto f = FileManager::OpenFile<VFilePtrReal>(writeFileName.c_str(),"w");
	if(f == nullptr)
	{
		outErr = "Unable to open file '" +writeFileName +"'!";
		return false;
	}
	result = udmData->SaveAscii(f,udm::AsciiSaveFlags::None);
	if(result == false)
	{
		outErr = "Unable to save UDM data!";
		return false;
	}
	return true;
}
bool Material::Save(std::string &outErr)
{
	auto mdlName = GetName();
	std::string absFileName;
	auto result = FileManager::FindAbsolutePath("materials/" +mdlName,absFileName);
	auto absolutePath = false;
	if(result == false)
		absFileName = mdlName;
	else
	{
		auto path = util::Path::CreateFile(absFileName);
		path.MakeRelative(util::get_program_path());
		absFileName = path.GetString();
		absolutePath = true;
	}
	return Save(absFileName,outErr,absolutePath);
}
std::optional<std::string> Material::GetAbsolutePath() const
{
	auto name = const_cast<Material*>(this)->GetName();
	if(name.empty())
		return {};
	std::string absPath = GetManager().GetRootMaterialLocation() +"\\";
	absPath += name;
	ufile::remove_extension_from_filename(absPath,g_knownMaterialFormats);
	absPath += ".wmi";
	if(FileManager::FindLocalPath(absPath,absPath) == false)
		return {};
	return absPath;
}
CallbackHandle Material::CallOnLoaded(const std::function<void(void)> &f) const
{
	if(IsLoaded())
	{
		f();
		return {};
	}
	m_callOnLoaded.push_back(FunctionCallback<>::Create(f));
	return m_callOnLoaded.back();
}
bool Material::IsLoaded() const {return umath::is_flag_set(m_stateFlags,StateFlags::Loaded);}

const TextureInfo *Material::GetDiffuseMap() const {return const_cast<Material*>(this)->GetDiffuseMap();}
TextureInfo *Material::GetDiffuseMap() {return m_texDiffuse;}

const TextureInfo *Material::GetAlbedoMap() const {return GetDiffuseMap();}
TextureInfo *Material::GetAlbedoMap() {return GetDiffuseMap();}

const TextureInfo *Material::GetNormalMap() const {return const_cast<Material*>(this)->GetNormalMap();}
TextureInfo *Material::GetNormalMap() {return m_texNormal;}

const TextureInfo *Material::GetGlowMap() const {return const_cast<Material*>(this)->GetGlowMap();}
TextureInfo *Material::GetGlowMap() {return m_texGlow;}

const TextureInfo *Material::GetAlphaMap() const {return const_cast<Material*>(this)->GetAlphaMap();}
TextureInfo *Material::GetAlphaMap() {return m_texAlpha;}

const TextureInfo *Material::GetParallaxMap() const {return const_cast<Material*>(this)->GetParallaxMap();}
TextureInfo *Material::GetParallaxMap() {return m_texParallax;}

const TextureInfo *Material::GetRMAMap() const {return const_cast<Material*>(this)->GetRMAMap();}
TextureInfo *Material::GetRMAMap() {return m_texRma;}

AlphaMode Material::GetAlphaMode() const {return m_alphaMode;}
float Material::GetAlphaCutoff() const
{
	auto alphaCutoff = 0.5f;
	const_cast<Material*>(this)->GetPropertyData()["alpha_cutoff"](alphaCutoff);
	return alphaCutoff;
}

void Material::SetColorFactor(const Vector4 &colorFactor) {GetPropertyData()["color_factor"] = colorFactor;}
Vector4 Material::GetColorFactor() const
{
	Vector4 colorFactor {1.f,1.f,1.f,1.f};
	const_cast<Material*>(this)->GetPropertyData()["color_factor"](colorFactor);
	return colorFactor;
}
void Material::SetBloomColorFactor(const Vector4 &bloomColorFactor) {GetPropertyData()["bloom_color_factor"] = bloomColorFactor;}
std::optional<Vector4> Material::GetBloomColorFactor() const
{
	Vector4 bloomColorFactor;
	if(!const_cast<Material*>(this)->GetPropertyData()["bloom_color_factor"](bloomColorFactor))
		return {};
	return bloomColorFactor;
}

void Material::SetName(const std::string &name) {m_name = name;}
const std::string &Material::GetName() {return m_name;}

bool Material::IsError() const {return umath::is_flag_set(m_stateFlags,StateFlags::Error);}
void Material::SetErrorFlag(bool set) {umath::set_flag(m_stateFlags,StateFlags::Error,set);}

const util::ShaderInfo *Material::GetShaderInfo() const {return m_shaderInfo.get();}
const std::string &Material::GetShaderIdentifier() const
{
	if(m_shaderInfo.expired() == false)
		return m_shaderInfo.get()->GetIdentifier();
	static std::string empty;
	return m_shader ? *m_shader : empty;
}

const TextureInfo *Material::GetTextureInfo(const std::string &key) const {return const_cast<Material*>(this)->GetTextureInfo(key);}

TextureInfo *Material::GetTextureInfo(const std::string &key)
{
	auto it = m_texInfos.find(key);
	if(it == m_texInfos.end())
		return nullptr;
	return &it->second;
}

udm::Element &Material::GetData() {return *m_data;}

std::ostream &operator<<(std::ostream &out,const Material &o)
{
	out<<"Material";
	out<<"[Index:"<<o.GetIndex()<<"]";
	out<<"[Name:"<<const_cast<Material&>(o).GetName()<<"]";
	out<<"[Shader:"<<o.GetShaderIdentifier()<<"]";
	out<<"[AlphaMode:"<<magic_enum::enum_name(o.GetAlphaMode())<<"]";
	out<<"[AlphaCutoff:"<<o.GetAlphaCutoff()<<"]";
	out<<"[ColorFactor:"<<o.GetColorFactor()<<"]";
	out<<"[Error:"<<o.IsError()<<"]";
	out<<"[Loaded:"<<o.IsLoaded()<<"]";
	return out;
}
