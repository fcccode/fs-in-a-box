#include "DirIntf.h"

#include "Logging.h"

using namespace FsBox::BlockTypes;

namespace FsBox
{

DirIntf::DirIntf(Container& container):
_blockReader(container),
_blockAllocator(container),
_fileIntf(container)
{
}

size_t DirIntf::GetMaxFileName()
{
	return 256;
}

BlockHandle DirIntf::GetRoot()
{
	ContainerHeader* containerHeader = _blockReader.Get<ContainerHeader>(0);
	if (containerHeader->rootDir)
	{
		return containerHeader->rootDir;
	}
	BlockHandle root = MakeDirHeader(0);
	containerHeader = _blockReader.Get<ContainerHeader>(0);
	return containerHeader->rootDir = root;
}

void DirIntf::EnumerateDir(BlockHandle dir, std::function<EnumDirEntriesFn> callback)
{
	DirHeader* pDirHeader = _blockReader.Get<DirHeader>(dir);
	if (!pDirHeader)
	{
		LOG_ERROR("%s", "Invalid dir handle");
		return;
	}
	BlockHandle dirEntry = pDirHeader->entryList;
	while (dirEntry)
	{
		DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
		if (!pDirEntry)
		{
			LOG_ERROR("%s", "Broken link in dir entry list");
			break;
		}
		std::string name = FileToString(pDirEntry->name);
		pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
		BlockHandle nextEntry = pDirEntry->next;
		if (!callback(dirEntry, pDirEntry, name))
		{
			break;
		}
		dirEntry = nextEntry;
	}
}

FileType DirIntf::GetDirEntryType(BlockHandle dir, const std::string& name)
{
	BlockHandle dirEntry = GetDirEntry(dir, name);
	return GetDirEntryType(dirEntry);
}

bool DirIntf::IsEmpty(BlockHandle dir)
{
	DirHeader* pDirHeader = _blockReader.Get<DirHeader>(dir);
	if (!pDirHeader)
	{
		LOG_ERROR("%s", "Invalid dir");
		return false;
	}
	return !pDirHeader->entryList;
}

BlockHandle DirIntf::OpenDir(BlockHandle parent, const std::string& name)
{
	BlockHandle dirEntry = GetDirEntry(parent, name);
	if (!dirEntry)
	{
		LOG_ERROR("%s does not exist", name.c_str());
		return 0;
	}
	if (GetDirEntryType(dirEntry) != FileType::Dir)
	{
		LOG_ERROR("%s is not dir", name.c_str());
		return 0;
	}
	return GetDirEntryBody(dirEntry);
}

BlockHandle DirIntf::CreateDir(BlockHandle parent, const std::string& nameStr)
{
	if (GetDirEntryType(parent, nameStr) != FileType::Unknown)
	{
		LOG_ERROR("%s already exists", nameStr.c_str());
		return 0;
	}
	BlockHandle name = StringToFile(nameStr);
	BlockHandle dir = MakeDirHeader(parent);
	if (!dir)
	{
		FreeFile(name);
	}
	BlockHandle dirEntry = MakeDirEntry(FileType::Dir, name, dir);
	if (!dirEntry)
	{
		FreeFile(name);
		FreeDirHeader(dir);
	}
	InsertDirEntry(parent, dirEntry);
	return dir;
}

bool DirIntf::DeleteDir(BlockHandle parent, const std::string& name)
{
	BlockHandle dirEntry = GetDirEntry(parent, name);
	if (!dirEntry)
	{
		LOG_ERROR("No such entry %s", name.c_str());
		return false;
	}
	FileType type = GetDirEntryType(dirEntry);
	if (type != FileType::Dir)
	{
		LOG_ERROR("%s is not dir",  name.c_str());
		return false;
	}
	BlockHandle dir = GetDirEntryBody(dirEntry);
	if (!IsEmpty(dir))
	{
		LOG_ERROR("Dir %s is not empty", name.c_str());
		return false;
	}
	FreeDirHeader(dir);
	DeleteDirEntry(parent, dirEntry);
	FreeDirEntry(dirEntry);
	return true;
}

BlockHandle DirIntf::OpenFile(BlockHandle parent, const std::string& nameStr)
{
	BlockHandle dirEntry = GetDirEntry(parent, nameStr);
	if (!dirEntry)
	{
		LOG_ERROR("%s does not exist", nameStr.c_str());
		return 0;
	}
	if (GetDirEntryType(dirEntry) != FileType::File)
	{
		LOG_ERROR("%s is not dir", nameStr.c_str());
		return 0;
	}
	return GetDirEntryBody(dirEntry);
}

BlockHandle DirIntf::CreateFile(BlockHandle parent, const std::string& nameStr)
{
	if (GetDirEntryType(parent, nameStr) != FileType::Unknown)
	{
		LOG_ERROR("%s already exists", nameStr.c_str());
		return 0;
	}
	BlockHandle name = StringToFile(nameStr);
	BlockHandle file = _fileIntf.Create();
	if (!file)
	{
		FreeFile(name);
	}
	BlockHandle dirEntry = MakeDirEntry(FileType::File, name, file);
	if (!dirEntry)
	{
		FreeFile(name);
		FreeFile(file);
	}
	InsertDirEntry(parent, dirEntry);
	return file;
}

bool DirIntf::DeleteFile(BlockHandle parent, const std::string& name)
{
	BlockHandle dirEntry = GetDirEntry(parent, name);
	if (!dirEntry)
	{
		LOG_ERROR("No such entry %s", name.c_str());
		return false;
	}
	FileType type = GetDirEntryType(dirEntry);
	if (type != FileType::File)
	{
		LOG_ERROR("%s is not file",  name.c_str());
		return false;
	}
	BlockHandle file = GetDirEntryBody(dirEntry);
	FreeFile(file);
	DeleteDirEntry(parent, dirEntry);
	FreeDirEntry(dirEntry);
	return true;
}

std::string DirIntf::FileToString(BlockHandle file)
{
	std::string result;
	auto size = _fileIntf.GetSize(file);
	if (size > GetMaxFileName())
	{
		LOG_ERROR("File is too big: %d, maximum allowed size is: %d", size, GetMaxFileName());
		return result;
	}
	if (size)
	{
		result.resize(static_cast<size_t>(size));
		_fileIntf.Read(file, &result[0], result.size(), 0);
	}
	return result;
}

BlockHandle DirIntf::StringToFile(const std::string& str)
{
	auto size = str.size();
	if (size > GetMaxFileName())
	{
		LOG_ERROR("String is too big: %d, maximum allowed size is: %d", size, GetMaxFileName());
		return 0;
	}
	BlockHandle file = _fileIntf.Create();
	if (size)
	{
		_fileIntf.Append(file, &str[0], str.size());
	}
	return file;
}

void DirIntf::FreeFile(BlockHandle file)
{
	_fileIntf.Delete(file);
}

BlockHandle DirIntf::GetDirEntry(BlockHandle dir, const std::string & name)
{
	BlockHandle result = 0;
	auto getCallback = [&name, &result](BlockHandle entry, BlockTypes::DirEntry* pDirEntry, const std::string& entryName)
	{
		if (name == entryName)
		{
			result = entry;
			return false;
		}
		return true;
	};
	EnumerateDir(dir, getCallback);
	return result;
}

BlockTypes::FileType DirIntf::GetDirEntryType(BlockHandle dirEntry)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return FileType::Unknown;
	}
	return pDirEntry->type;
}

BlockHandle DirIntf::GetDirEntryBody(BlockHandle dirEntry)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return 0;
	}
	return pDirEntry->body;
}

BlockHandle DirIntf::GetDirEntryName(BlockHandle dirEntry)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return 0;
	}
	return pDirEntry->name;
}

BlockHandle DirIntf::GetNextDirEntry(BlockHandle dirEntry)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return 0;
	}
	return pDirEntry->next;
}

BlockHandle DirIntf::GetPrevDirEntry(BlockHandle dirEntry)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return 0;
	}
	return pDirEntry->prev;
}

void DirIntf::SetNextDirEntry(BlockHandle dirEntry, BlockHandle next)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return;
	}
	pDirEntry->next = next;
}

void DirIntf::SetPrevDirEntry(BlockHandle dirEntry, BlockHandle prev)
{
	DirEntry* pDirEntry = _blockReader.Get<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid dir entry");
		return;
	}
	pDirEntry->prev = prev;
}

void DirIntf::InsertDirEntry(BlockHandle dir, BlockHandle dirEntry)
{
	DirHeader* pDirHeader = _blockReader.Get<DirHeader>(dir);
	if (!pDirHeader)
	{
		LOG_ERROR("%s", "Invalid dir");
		return ;
	}
	BlockHandle oldHead = pDirHeader->entryList;
	// prepare new entry
	SetPrevDirEntry(dirEntry, 0);
	SetNextDirEntry(dirEntry, oldHead);
	// fix prev links in old head
	if (oldHead)
	{
		SetPrevDirEntry(oldHead, dirEntry);
	}
	pDirHeader = _blockReader.Get<DirHeader>(dir);
	pDirHeader->entryList = dirEntry;
}

void DirIntf::DeleteDirEntry(BlockHandle dir, BlockHandle dirEntry)
{
	DirHeader* pDirHeader = _blockReader.Get<DirHeader>(dir);
	if (!pDirHeader)
	{
		LOG_ERROR("%s", "Invalid dir");
		return ;
	}
	if (pDirHeader->entryList == dirEntry)
	{
		BlockHandle nextEntry = GetNextDirEntry(dirEntry);
		pDirHeader = _blockReader.Get<DirHeader>(dir);
		pDirHeader->entryList = nextEntry;
		SetPrevDirEntry(nextEntry, 0);
	}
	else
	{
		BlockHandle prev = GetPrevDirEntry(dirEntry);
		BlockHandle next = GetNextDirEntry(dirEntry);
		if (prev)
		{
			SetNextDirEntry(prev, next);
		}
		if (next)
		{
			SetPrevDirEntry(next, prev);
		}
	}
}

BlockHandle DirIntf::MakeDirHeader(BlockHandle parent)
{
	BlockHandle dirHeader = _blockAllocator.Allocate(sizeof(DirHeader));
	DirHeader* pDirHeader = _blockReader.CastTo<DirHeader>(dirHeader);
	if (!pDirHeader)
	{
		LOG_ERROR("%s", "Invalid allocation");
		return 0;
	}
	pDirHeader->parent = parent;
	pDirHeader->entryList = 0;
	return dirHeader;
}

void DirIntf::FreeDirHeader(BlockHandle dir)
{
	_blockAllocator.Free(dir);
}

BlockHandle DirIntf::MakeDirEntry(BlockTypes::FileType type, BlockHandle name, BlockHandle payload)
{
	BlockHandle dirEntry = _blockAllocator.Allocate(sizeof(DirEntry));
	DirEntry* pDirEntry = _blockReader.CastTo<DirEntry>(dirEntry);
	if (!pDirEntry)
	{
		LOG_ERROR("%s", "Invalid allocation");
		return 0;
	}
	pDirEntry->type = type;
	pDirEntry->name = name;
	pDirEntry->body = payload;
	return dirEntry;
}

void DirIntf::FreeDirEntry(BlockHandle dirEntry)
{
	BlockHandle name = GetDirEntryName(dirEntry);
	if (name)
	{
		_blockAllocator.Free(name);
	}
	// body deallocation logic is more complicated and should be implemented
	// by the caller
	_blockAllocator.Free(dirEntry);
}


}//namespace FsBox