#include "MMF.h"

#include "FsUtils.h"
#include "Logging.h"

#include <boost/static_assert.hpp>

using boost::iostreams::mapped_file_base;
using boost::iostreams::mapped_file;

namespace FsBox
{

MemoryMappedFile::MemoryMappedFile(size_t memViewSize)
:_fileOffset(0),
_fileSize(0),
_alignment(mapped_file::alignment()),
_memViewSize(memViewSize)
{
}

MemoryMappedFile::~MemoryMappedFile()
{
	try
	{
		if (_mf.is_open())
		{
			_mf.close();
		}
	}
	catch(const std::ios::failure& ex)
	{
		LOG_ERROR("%s", ex.what());
	}
}

bool MemoryMappedFile::Open(const std::string& fileName, stream_offset offset)
{
	_fileName = fileName;
	if (!PeekFile(fileName))
	{
		LOG_ERROR("Cannot access file: %s", fileName.c_str());
		return false;
	}
	size_t viewSize = (_fileSize > _memViewSize) ? _memViewSize : static_cast<size_t>(_fileSize);
	_fileOffset = (offset / _alignment) * _alignment;
	if (_fileOffset + viewSize > _fileSize)
	{
		viewSize = _fileSize - _fileOffset;
	}
	try
	{
		LOG_DEBUG("About to open: %s, %d", fileName.c_str(), viewSize);
		_mf.open(fileName, BOOST_IOS::in | BOOST_IOS::out, viewSize, _fileOffset);
	}
	catch(const std::ios::failure& ex)
	{
		LOG_ERROR("%s", ex.what());
		return false;
	}
	return _mf.is_open();
}

bool MemoryMappedFile::PeekFile(const std::string& fileName)
{
	if (!FsUtils::FileExists(fileName))
	{
		return false;
	}
	_fileSize = FsUtils::GetFileSize(fileName);
	return true;
}

bool MemoryMappedFile::IsOpened() const
{
	return _mf.is_open();
}

void MemoryMappedFile::Close()
{
	try
	{
		_mf.close();
	}
	catch(const std::ios::failure& ex)
	{
		LOG_ERROR("%s", ex.what());
	}
	_fileSize = 0;
	_fileOffset = 0;
}

char * MemoryMappedFile::GetData()
{
	return _mf.data();
}

size_t MemoryMappedFile::GetDataSize() const
{
	return std::min(_memViewSize, _mf.size());
}

size_t MemoryMappedFile::GetMaxViewSize() const
{
	return _memViewSize;
}

stream_offset MemoryMappedFile::GetFileSize() const
{
	return _fileSize;
}

stream_offset MemoryMappedFile::GetDataOffset() const
{
	return _fileOffset;
}

bool MemoryMappedFile::Remap(stream_offset pos)
{
	if (pos >= _fileSize)
	{
		LOG_ERROR("%s", "Trying to rewind outside of file");
		return false;
	}
	Close();
	return Open(_fileName, pos);
}

bool MemoryMappedFile::Resize(stream_offset size)
{
	try
	{
		_mf.resize(size);
	}
	catch(const std::ios::failure& ex)
	{
		//Workaround bug in boost::iostreams::mapped_file
		(void)ex;
		Close();
		return Open(_fileName, 0);
	}
	_fileSize = size;
	return true;
}

}//namespace FsBox