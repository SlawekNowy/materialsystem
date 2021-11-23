/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <prosper_context.hpp>
#include "texturemanager/texturemanager.h"
#include "texturemanager/texturequeue.h"
#include "texturemanager/loadimagedata.h"
#include "sharedutils/functioncallback.h"
#include "textureinfo.h"
#include "texturemanager/texture.h"
#include <materialmanager.h>
#include <prosper_util.hpp>
#include <image/prosper_sampler.hpp>
#include <mathutil/glmutil.h>
#include <gli/gli.hpp>
#include <gli/texture2d.hpp>
#include <sharedutils/util_string.h>
#include <sharedutils/util_file.h>
#include <sharedutils/util_ifile.hpp>
#ifndef DISABLE_VTF_SUPPORT
#include <Proc.h>
#endif

decltype(TextureManager::MAX_TEXTURE_COUNT) TextureManager::MAX_TEXTURE_COUNT = 4096;
#pragma optimize("",off)
#ifndef DISABLE_VTF_SUPPORT
static vlVoid vtf_read_close() {}
static vlBool vtf_read_open() {return true;}
static vlUInt vtf_read_read(vlVoid *buf,vlUInt bytes,vlVoid *handle)
{
	if(handle == nullptr)
		return -1;
	auto &f = *static_cast<ufile::IFile*>(handle);
	return static_cast<vlUInt>(f.Read(buf,bytes));
}
static vlUInt vtf_read_seek(vlLong offset,VLSeekMode whence,vlVoid *handle)
{
	if(handle == nullptr)
		return -1;
	auto &f = *static_cast<ufile::IFile*>(handle);
	f.Seek(offset,static_cast<ufile::IFile::Whence>(whence));
	return f.Tell();
}
static vlUInt vtf_read_size(vlVoid *handle)
{
	if(handle == nullptr)
		return 0;
	auto &f = *static_cast<ufile::IFile*>(handle);
	return f.GetSize();
}
static vlUInt vtf_read_tell(vlVoid *handle)
{
	if(handle == nullptr)
		return -1;
	auto &f = *static_cast<ufile::IFile*>(handle);
	return f.Tell();
}
#endif

////////////////////////////

#if 0
#include "texturemanager/load/texture_loader.hpp"
#include "texturemanager/load/texture_format_handler.hpp"
#include "texturemanager/load/handlers/format_handler_gli.hpp"
#include "texturemanager/load/handlers/format_handler_uimg.hpp"
#include "texturemanager/load/handlers/format_handler_vtex.hpp"
#include "texturemanager/load/handlers/format_handler_vtf.hpp"
#include <fsys/ifile.hpp>
#undef AddJob
static void test_texture_loader(prosper::IPrContext &context)
{
	msys::TextureLoader loader {context};
	auto gliHandler = []() -> std::unique_ptr<msys::ITextureFormatHandler> {
		return std::make_unique<msys::TextureFormatHandlerGli>();
	};
	loader.RegisterFormatHandler("ktx",gliHandler);
	loader.RegisterFormatHandler("dds",gliHandler);

	auto uimgHandler = []() -> std::unique_ptr<msys::ITextureFormatHandler> {
		return std::make_unique<msys::TextureFormatHandlerUimg>();
	};
	loader.RegisterFormatHandler("png",uimgHandler);
	loader.RegisterFormatHandler("tga",uimgHandler);
	loader.RegisterFormatHandler("jpg",uimgHandler);
	loader.RegisterFormatHandler("bmp",uimgHandler);
	loader.RegisterFormatHandler("psd",uimgHandler);
	loader.RegisterFormatHandler("gif",uimgHandler);
	loader.RegisterFormatHandler("hdr",uimgHandler);
	loader.RegisterFormatHandler("pic",uimgHandler);

	loader.RegisterFormatHandler("vtf",[]() -> std::unique_ptr<msys::ITextureFormatHandler> {
		return std::make_unique<msys::TextureFormatHandlerVtf>();
	});
	loader.RegisterFormatHandler("vtex_c",[]() -> std::unique_ptr<msys::ITextureFormatHandler> {
		return std::make_unique<msys::TextureFormatHandlerVtex>();
	});

	loader.SetAllowMultiThreadedGpuResourceAllocation(true); // TODO: Turn off for OpenGL

	std::string basePath = util::get_program_path() +'/';
	auto addTexture = [&basePath,&loader,&context](const std::string &texPath,const std::string &identifier) {
		auto fp = filemanager::open_system_file(basePath +"/" +texPath,filemanager::FileMode::Binary | filemanager::FileMode::Read);
		auto f = std::make_shared<fsys::File>(fp);
		std::string ext;
		ufile::get_extension(texPath,&ext);
		loader.AddJob(context,identifier,ext,f);
	};
	addTexture("materials/error.dds","error");

	uint32_t numComplete = 0;
	auto t = std::chrono::high_resolution_clock::now();
	for(;;)
	{
		loader.Poll([&numComplete,&t](const util::AssetLoadJob &job) {
			auto dtQueue = job.completionTime -job.queueStartTime;
			auto dtTask = job.completionTime -job.taskStartTime;
			std::cout<<job.identifier<<" has been loaded!"<<std::endl;
			std::cout<<"Time since job has been queued to completion: "<<(dtQueue.count() /1'000'000'000.0)<<std::endl;
			std::cout<<"Time since task has been started to completion: "<<(dtTask.count() /1'000'000'000.0)<<std::endl;
			if(++numComplete == 7)
			{
				auto dt = std::chrono::high_resolution_clock::now() -t;
				std::cout<<"Total completion time: "<<(dt.count() /1'000'000'000.0)<<std::endl;
			}
		},[](const util::AssetLoadJob &job) {
			std::cout<<job.identifier<<" has failed!"<<std::endl;
		});
	}
}
#endif

////////////////////////////

TextureManager::LoadInfo::LoadInfo()
	: mipmapLoadMode(TextureMipmapMode::Load)
{}

TextureManager::TextureManager(prosper::IPrContext &context)
	: m_wpContext(context.shared_from_this()),m_textureSampler(nullptr),
	m_textureSamplerNoMipmap(nullptr),m_bThreadActive(false)
{
	auto samplerCreateInfo = prosper::util::SamplerCreateInfo {};
	TextureManager::SetupSamplerMipmapMode(samplerCreateInfo,TextureMipmapMode::Load);
	m_textureSampler = context.CreateSampler(samplerCreateInfo);

	samplerCreateInfo = {};
	TextureManager::SetupSamplerMipmapMode(samplerCreateInfo,TextureMipmapMode::Ignore);
	m_textureSamplerNoMipmap = context.CreateSampler(samplerCreateInfo);
#ifndef DISABLE_VTF_SUPPORT
	vlSetProc(PROC_READ_CLOSE,reinterpret_cast<void*>(vtf_read_close));
	vlSetProc(PROC_READ_OPEN,reinterpret_cast<void*>(vtf_read_open));
	vlSetProc(PROC_READ_READ,reinterpret_cast<void*>(vtf_read_read));
	vlSetProc(PROC_READ_SEEK,reinterpret_cast<void*>(vtf_read_seek));
	vlSetProc(PROC_READ_SIZE,reinterpret_cast<void*>(vtf_read_size));
	vlSetProc(PROC_READ_TELL,reinterpret_cast<void*>(vtf_read_tell));
#endif
	// test_texture_loader(context);
}

TextureManager::~TextureManager()
{
	Clear();
}

bool TextureManager::HasWork()
{
	if(m_bThreadActive == false)
		return false;
	if(m_bBusy)
		return true;
	m_loadMutex->lock();
		auto hasLoadItem = (m_loadQueue.empty() == false);
	m_loadMutex->unlock();
	if(hasLoadItem)
		return true;
	m_queueMutex->lock();
		auto hasInitItem = (m_initQueue.empty() == false);
	m_queueMutex->unlock();
	return hasInitItem;
}

void TextureManager::WaitForTextures()
{
	while(HasWork())
		Update();
	Update();
}

void TextureManager::SetupSamplerMipmapMode(prosper::util::SamplerCreateInfo &createInfo,TextureMipmapMode mode)
{
	switch(mode)
	{
		case TextureMipmapMode::Ignore:
		{
			createInfo.minFilter = prosper::Filter::Nearest;
			createInfo.magFilter = prosper::Filter::Nearest;
			createInfo.mipmapMode = prosper::SamplerMipmapMode::Nearest;
			createInfo.minLod = 0.f;
			createInfo.maxLod = 0.f;
			break;
		}
		default:
		{
			createInfo.minFilter = prosper::Filter::Linear;
			createInfo.magFilter = prosper::Filter::Linear;
			createInfo.mipmapMode = prosper::SamplerMipmapMode::Linear;
			break;
		}
	}
}

void TextureManager::RegisterCustomSampler(const std::shared_ptr<prosper::ISampler> &sampler) {m_customSamplers.push_back(sampler);}
const std::vector<std::weak_ptr<prosper::ISampler>> &TextureManager::GetCustomSamplers() const {return m_customSamplers;}

prosper::IPrContext &TextureManager::GetContext() const {return *m_wpContext.lock();}

void TextureManager::SetTextureFileHandler(const std::function<VFilePtr(const std::string&)> &fileHandler) {m_texFileHandler = fileHandler;}

std::shared_ptr<Texture> TextureManager::CreateTexture(const std::string &name,prosper::Texture &texture)
{
	auto it = std::find_if(m_textures.begin(),m_textures.end(),[&name](std::shared_ptr<Texture> &tex) {
		return (tex->GetName() == name) ? true : false;
	});
	if(it != m_textures.end())
		return *it;
	auto tex = std::make_shared<Texture>(GetContext());
	tex->SetVkTexture(texture.shared_from_this());
	tex->SetName(name);
	m_textures.push_back(tex);
	return tex;
}

std::shared_ptr<Texture> TextureManager::GetErrorTexture() {return m_error;}

/*std::shared_ptr<Texture> TextureManager::CreateTexture(prosper::Context &context,const std::string &name,unsigned int w,unsigned int h)
{
	auto it = std::find_if(m_textures.begin(),m_textures.end(),[&name](std::shared_ptr<Texture> &tex) {
		return (tex->name == name) ? true : false;
	});
	if(it != m_textures.end())
		return *it;
	auto dev = context.GetDevice()->shared_from_this();
	prosper::util::ImageCreateInfo imgCreateInfo {};
	imgCreateInfo.
	auto img = prosper::util::create_image(dev,imgCreateInfo);

	prosper::util::TextureCreateInfo texCreateInfo {};
	prosper::util::ImageViewCreateInfo imgViewCreateInfo {};
	prosper::util::SamplerCreateInfo samplerCreateInfo {};

	prosper::util::create_texture(dev,texCreateInfo,img,&imgViewCreateInfo,&samplerCreateInfo);
	//vk::Format::eR16G16B16A16Sfloat
	try
	{
		auto vkTex = Vulkan::Texture::Create(context,w,h);
		return CreateTexture(name,vkTex);
	}
	catch(Vulkan::Exception &e)
	{
		return nullptr;
	}
}*/

std::shared_ptr<Texture> TextureManager::GetTexture(const std::string &name)
{
	auto nameNoExt = name;
	static std::vector<std::string> supportedExtensions;
	if(supportedExtensions.empty())
	{
		auto &supportedFormats = MaterialManager::get_supported_image_formats();
		supportedExtensions.reserve(supportedFormats.size());
		for(auto &format : supportedFormats)
			supportedExtensions.push_back(format.extension);
	}
	ufile::remove_extension_from_filename(nameNoExt,supportedExtensions);
	auto it = std::find_if(m_textures.begin(),m_textures.end(),[&nameNoExt](const std::shared_ptr<Texture> &tex) {
		auto texNoExt = tex->GetName();
		ufile::remove_extension_from_filename(texNoExt,supportedExtensions);
		return FileManager::ComparePath(nameNoExt,texNoExt);
	});
	return (it != m_textures.end()) ? *it : nullptr;
}

void TextureManager::ReloadTextures(const LoadInfo &loadInfo)
{
	for(unsigned int i=0;i<m_textures.size();i++)
	{
		auto &texture = m_textures[i];
		ReloadTexture(*texture,loadInfo);
	}
}

void TextureManager::ReloadTexture(uint32_t texId,const LoadInfo &loadInfo)
{
	if(texId >= m_textures.size())
		return;
	auto &texture = m_textures[texId];
	if(texture->HasValidVkTexture() == false)
		return;
	auto &tex = texture->GetVkTexture();
	auto &context = tex->GetContext();
	auto sampler = tex->GetSampler();
	auto ptr = std::static_pointer_cast<void>(texture->shared_from_this());
	Load(context,texture->GetName(),loadInfo,&ptr);
	texture = std::static_pointer_cast<Texture>(ptr);
}

void TextureManager::ReloadTexture(Texture &texture,const LoadInfo &loadInfo)
{
	auto it = std::find_if(m_textures.begin(),m_textures.end(),[&texture](const std::shared_ptr<Texture> &texOther) {
		return (texOther.get() == &texture) ? true : false;
	});
	if(it == m_textures.end())
		return;
	ReloadTexture(it -m_textures.begin(),loadInfo);
}

void TextureManager::SetErrorTexture(const std::shared_ptr<Texture> &tex)
{
	if(m_error)
	{
		auto flags = m_error->GetFlags();
		umath::set_flag(flags,Texture::Flags::Error,false);
		m_error->SetFlags(flags);
	}
	m_error = tex;
	if(tex)
		tex->AddFlags(Texture::Flags::Error);
}

uint32_t TextureManager::Clear()
{
	if(m_threadLoad != nullptr)
	{
		m_activeMutex->lock();
			m_bThreadActive = false;
		m_activeMutex->unlock();
		m_queueVar->notify_one();
		m_threadLoad->join();
		m_threadLoad = nullptr;
		m_queueVar = nullptr;
		m_queueMutex = nullptr;
		m_lockMutex = nullptr;
		m_activeMutex = nullptr;
		m_loadMutex = nullptr;
	}
	while(!m_loadQueue.empty())
		m_loadQueue.pop();
	auto n = m_textures.size();
	m_textures.clear();
	m_textureSampler = nullptr;
	m_textureSamplerNoMipmap = nullptr;
	m_error = nullptr;
	return n;
}
uint32_t TextureManager::ClearUnused()
{
	uint32_t n = 0;
	for(auto &tex : m_textures)
	{
		if(tex.use_count() == 1 && tex->GetVkTexture())
		{
			tex->Reset();
			++n;
		}
	}
	return n;
}

std::shared_ptr<prosper::ISampler> &TextureManager::GetTextureSampler() {return m_textureSampler;}

std::shared_ptr<Texture> TextureManager::FindTexture(const std::string &imgFile,bool *bLoading)
{
	std::string cache;
	return FindTexture(imgFile,&cache,bLoading);
}
std::shared_ptr<Texture> TextureManager::FindTexture(const std::string &imgFile,bool bLoadedOnly)
{
	auto bLoading = false;
	auto r = FindTexture(imgFile,&bLoading);
	if(bLoadedOnly == true && bLoading == true)
		return nullptr;
	return r;
}

std::shared_ptr<Texture> TextureManager::FindTexture(const std::string &imgFile,std::string *cache,bool *bLoading)
{
	if(bLoading != nullptr)
		*bLoading = false;
	auto pathCache = FileManager::GetNormalizedPath(imgFile);
	auto rBr = pathCache.find_last_of('\\');
	if(rBr != std::string::npos)
	{
		auto ext = pathCache.find('.',rBr +1);
		if(ext != std::string::npos)
			pathCache = pathCache.substr(0,ext);
	}
	*cache = pathCache;
	const auto find = [&pathCache](decltype(m_textures.front()) &tex) {
		return (tex->GetName() == pathCache) ? true : false;
	};
	auto itTex = std::find_if(m_textures.begin(),m_textures.end(),find);
	if(itTex != m_textures.end())
		return *itTex;
	auto itTmp = std::find_if(m_texturesTmp.begin(),m_texturesTmp.end(),find);
	if(itTmp != m_texturesTmp.end())
	{
		if(bLoading != nullptr)
			*bLoading = true;
		return *itTmp;
	}
	return nullptr;
}
#pragma optimize("",on)
