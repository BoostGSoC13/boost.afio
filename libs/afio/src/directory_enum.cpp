/* FastDirectoryEnumerator
Enumerates very, very large directories quickly by directly using kernel syscalls. For POSIX and Windows.
(C) 2013 Niall Douglas http://www.nedprod.com/
File created: Aug 2013
*/

#define __USE_XOPEN2K8 // Turns on timespecs in Linux
#include "../../../boost/afio/directory_monitor.hpp"
#include "../../../boost/afio/detail/Undoer.hpp"
#include <sys/stat.h>
#ifdef WIN32
#include <Windows.h>
#include "boost/thread/tss.hpp"
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>     /* Defines DT_* constants */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <fnmatch.h>
#endif

namespace boost{
	namespace afio{

#ifdef WIN32
#define TIMESPEC_TO_FILETIME_OFFSET (((long long)27111902 << 32) + (long long)3577643008)

static inline struct timespec to_timespec(LARGE_INTEGER time)
{
	struct timespec ts;
	ts.tv_sec = ((time.QuadPart - TIMESPEC_TO_FILETIME_OFFSET) / 10000000);
	ts.tv_nsec = (long)((time.QuadPart - TIMESPEC_TO_FILETIME_OFFSET - ((long long)ts.tv_sec * (long long)10000000)) * 100);
	return ts;
}

static inline uint16_t to_st_type(ULONG FileAttributes, uint16_t mode=0)
{
	if(FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
		mode|=S_IFLNK;
	else if(FileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		mode|=S_IFDIR;
	else
		mode|=S_IFREG;
	return mode;
}
#endif

have_metadata_flags directory_entry::metadata_supported() BOOST_NOEXCEPT_OR_NOTHROW
{
	have_metadata_flags ret; ret.value=0;
#ifdef WIN32
	ret.have_dev=0;
	ret.have_ino=1;        // FILE_INTERNAL_INFORMATION, enumerated
	ret.have_type=1;       // FILE_BASIC_INFORMATION, enumerated
	ret.have_mode=0;
	ret.have_nlink=1;      // FILE_STANDARD_INFORMATION
	ret.have_uid=0;
	ret.have_gid=0;
	ret.have_rdev=0;
	ret.have_atim=1;       // FILE_BASIC_INFORMATION, enumerated
	ret.have_mtim=1;       // FILE_BASIC_INFORMATION, enumerated
	ret.have_ctim=1;       // FILE_BASIC_INFORMATION, enumerated
	ret.have_size=1;       // FILE_STANDARD_INFORMATION, enumerated
	ret.have_allocated=1;  // FILE_STANDARD_INFORMATION, enumerated
	ret.have_blocks=1;
	ret.have_blksize=1;    // FILE_ALIGNMENT_INFORMATION
	ret.have_flags=0;
	ret.have_gen=0;
	ret.have_birthtim=1;   // FILE_BASIC_INFORMATION, enumerated
#elif defined(__linux__)
	ret.have_dev=1;
	ret.have_ino=1;
	ret.have_type=1;
	ret.have_mode=1;
	ret.have_nlink=1;
	ret.have_uid=1;
	ret.have_gid=1;
	ret.have_rdev=1;
	ret.have_atim=1;
	ret.have_mtim=1;
	ret.have_ctim=1;
	ret.have_size=1;
	ret.have_allocated=1;
	ret.have_blocks=1;
	ret.have_blksize=1;
	// Sadly these must wait until someone fixes the Linux stat() call e.g. the xstat() proposal.
	ret.have_flags=0;
	ret.have_gen=0;
	ret.have_birthtim=0;
	// According to http://computer-forensics.sans.org/blog/2011/03/14/digital-forensics-understanding-ext4-part-2-timestamps
	// ext4 keeps birth time at offset 144 to 151 in the inode. If we ever got round to it, birthtime could be hacked.
#else
	// Kinda assumes FreeBSD or OS X really ...
	ret.have_dev=1;
	ret.have_ino=1;
	ret.have_type=1;
	ret.have_mode=1;
	ret.have_nlink=1;
	ret.have_uid=1;
	ret.have_gid=1;
	ret.have_rdev=1;
	ret.have_atim=1;
	ret.have_mtim=1;
	ret.have_ctim=1;
	ret.have_size=1;
	ret.have_allocated=1;
	ret.have_blocks=1;
	ret.have_blksize=1;
#define HAVE_STAT_FLAGS
	ret.have_flags=1;
#define HAVE_STAT_GEN
	ret.have_gen=1;
#define HAVE_BIRTHTIMESPEC
	ret.have_birthtim=1;
#endif
	return ret;
}

void directory_entry::_int_fetch(have_metadata_flags wanted, std::filesystem::path prefix)
{
#ifdef WIN32
	// From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/FILE_INFORMATION_CLASS.html
	typedef enum _FILE_INFORMATION_CLASS {
		FileDirectoryInformation                 = 1,
		FileFullDirectoryInformation,
		FileBothDirectoryInformation,
		FileBasicInformation,
		FileStandardInformation,
		FileInternalInformation,
		FileEaInformation,
		FileAccessInformation,
		FileNameInformation,
		FileRenameInformation,
		FileLinkInformation,
		FileNamesInformation,
		FileDispositionInformation,
		FilePositionInformation,
		FileFullEaInformation,
		FileModeInformation,
		FileAlignmentInformation,
		FileAllInformation,
		FileAllocationInformation,
		FileEndOfFileInformation,
		FileAlternateNameInformation,
		FileStreamInformation,
		FilePipeInformation,
		FilePipeLocalInformation,
		FilePipeRemoteInformation,
		FileMailslotQueryInformation,
		FileMailslotSetInformation,
		FileCompressionInformation,
		FileObjectIdInformation,
		FileCompletionInformation,
		FileMoveClusterInformation,
		FileQuotaInformation,
		FileReparsePointInformation,
		FileNetworkOpenInformation,
		FileAttributeTagInformation,
		FileTrackingInformation,
		FileIdBothDirectoryInformation,
		FileIdFullDirectoryInformation,
		FileValidDataLengthInformation,
		FileShortNameInformation,
		FileIoCompletionNotificationInformation,
		FileIoStatusBlockRangeInformation,
		FileIoPriorityHintInformation,
		FileSfioReserveInformation,
		FileSfioVolumeInformation,
		FileHardLinkInformation,
		FileProcessIdsUsingFileInformation,
		FileNormalizedNameInformation,
		FileNetworkPhysicalNameInformation,
		FileIdGlobalTxDirectoryInformation,
		FileIsRemoteDeviceInformation,
		FileAttributeCacheInformation,
		FileNumaNodeInformation,
		FileStandardLinkInformation,
		FileRemoteProtocolInformation,
		FileMaximumInformation
	} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

	typedef enum  { 
	  FileFsVolumeInformation       = 1,
	  FileFsLabelInformation        = 2,
	  FileFsSizeInformation         = 3,
	  FileFsDeviceInformation       = 4,
	  FileFsAttributeInformation    = 5,
	  FileFsControlInformation      = 6,
	  FileFsFullSizeInformation     = 7,
	  FileFsObjectIdInformation     = 8,
	  FileFsDriverPathInformation   = 9,
	  FileFsVolumeFlagsInformation  = 10,
	  FileFsSectorSizeInformation   = 11
	} FS_INFORMATION_CLASS;
#ifndef NTSTATUS
#define NTSTATUS LONG
#endif

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff550671(v=vs.85).aspx
	typedef struct _IO_STATUS_BLOCK {
		union {
			NTSTATUS Status;
			PVOID    Pointer;
		};
		ULONG_PTR Information;
	} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

	// From http://msdn.microsoft.com/en-us/library/windows/desktop/aa380518(v=vs.85).aspx
	typedef struct _LSA_UNICODE_STRING {
	  USHORT Length;
	  USHORT MaximumLength;
	  PWSTR  Buffer;
	} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff557749(v=vs.85).aspx
	typedef struct _OBJECT_ATTRIBUTES {
	  ULONG           Length;
	  HANDLE          RootDirectory;
	  PUNICODE_STRING ObjectName;
	  ULONG           Attributes;
	  PVOID           SecurityDescriptor;
	  PVOID           SecurityQualityOfService;
	}  OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

	typedef struct _RTLP_CURDIR_REF
	{
		LONG RefCount;
		HANDLE Handle;
	} RTLP_CURDIR_REF, *PRTLP_CURDIR_REF;

	typedef struct RTL_RELATIVE_NAME_U {
		UNICODE_STRING RelativeName;
		HANDLE ContainingDirectory;
		PRTLP_CURDIR_REF CurDirRef;
	} RTL_RELATIVE_NAME_U, *PRTL_RELATIVE_NAME_U;

	// From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtQueryInformationFile.html
	// and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567052(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtQueryInformationFile_t)(
		/*_In_*/   HANDLE FileHandle,
		/*_Out_*/  PIO_STATUS_BLOCK IoStatusBlock,
		/*_Out_*/  PVOID FileInformation,
		/*_In_*/   ULONG Length,
		/*_In_*/   FILE_INFORMATION_CLASS FileInformationClass
		);

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff567070(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtQueryVolumeInformationFile_t)(
		/*_In_*/   HANDLE FileHandle,
		/*_Out_*/  PIO_STATUS_BLOCK IoStatusBlock,
		/*_Out_*/  PVOID FsInformation,
		/*_In_*/   ULONG Length,
		/*_In_*/   FS_INFORMATION_CLASS FsInformationClass
		);

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff566492(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtOpenDirectoryObject_t)(
	  /*_Out_*/  PHANDLE DirectoryHandle,
	  /*_In_*/   ACCESS_MASK DesiredAccess,
	  /*_In_*/   POBJECT_ATTRIBUTES ObjectAttributes
	);


	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff567011(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtOpenFile_t)(
	  /*_Out_*/  PHANDLE FileHandle,
	  /*_In_*/   ACCESS_MASK DesiredAccess,
	  /*_In_*/   POBJECT_ATTRIBUTES ObjectAttributes,
	  /*_Out_*/  PIO_STATUS_BLOCK IoStatusBlock,
	  /*_In_*/   ULONG ShareAccess,
	  /*_In_*/   ULONG OpenOptions
	);

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff566424(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtCreateFile_t)(
	  /*_Out_*/     PHANDLE FileHandle,
	  /*_In_*/      ACCESS_MASK DesiredAccess,
	  /*_In_*/      POBJECT_ATTRIBUTES ObjectAttributes,
	  /*_Out_*/     PIO_STATUS_BLOCK IoStatusBlock,
	  /*_In_opt_*/  PLARGE_INTEGER AllocationSize,
	  /*_In_*/      ULONG FileAttributes,
	  /*_In_*/      ULONG ShareAccess,
	  /*_In_*/      ULONG CreateDisposition,
	  /*_In_*/      ULONG CreateOptions,
	  /*_In_opt_*/  PVOID EaBuffer,
	  /*_In_*/      ULONG EaLength
	);

	typedef NTSTATUS (NTAPI *NtClose_t)(
	  /*_Out_*/  HANDLE FileHandle
	);

	typedef BOOLEAN (NTAPI *RtlDosPathNameToNtPathName_U_t)(
                             PCWSTR DosName,
                             PUNICODE_STRING NtName,
                             PCWSTR *PartName,
                             PRTL_RELATIVE_NAME_U RelativeName);

	// From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtQueryDirectoryFile.html
	// and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567047(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtQueryDirectoryFile_t)(
		/*_In_*/      HANDLE FileHandle,
		/*_In_opt_*/  HANDLE Event,
		/*_In_opt_*/  void *ApcRoutine,
		/*_In_opt_*/  PVOID ApcContext,
		/*_Out_*/     PIO_STATUS_BLOCK IoStatusBlock,
		/*_Out_*/     PVOID FileInformation,
		/*_In_*/      ULONG Length,
		/*_In_*/      FILE_INFORMATION_CLASS FileInformationClass,
		/*_In_*/      BOOLEAN ReturnSingleEntry,
		/*_In_opt_*/  PUNICODE_STRING FileName,
		/*_In_*/      BOOLEAN RestartScan
		);

	typedef struct _FILE_BASIC_INFORMATION {
	  LARGE_INTEGER CreationTime;
	  LARGE_INTEGER LastAccessTime;
	  LARGE_INTEGER LastWriteTime;
	  LARGE_INTEGER ChangeTime;
	  ULONG         FileAttributes;
	} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

	typedef struct _FILE_STANDARD_INFORMATION {
	  LARGE_INTEGER AllocationSize;
	  LARGE_INTEGER EndOfFile;
	  ULONG         NumberOfLinks;
	  BOOLEAN       DeletePending;
	  BOOLEAN       Directory;
	} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

	typedef struct _FILE_INTERNAL_INFORMATION {
	  LARGE_INTEGER IndexNumber;
	} FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

	typedef struct _FILE_EA_INFORMATION {
	  ULONG EaSize;
	} FILE_EA_INFORMATION, *PFILE_EA_INFORMATION;

	typedef struct _FILE_ACCESS_INFORMATION {
	  ACCESS_MASK AccessFlags;
	} FILE_ACCESS_INFORMATION, *PFILE_ACCESS_INFORMATION;

	typedef struct _FILE_POSITION_INFORMATION {
	  LARGE_INTEGER CurrentByteOffset;
	} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

	typedef struct _FILE_MODE_INFORMATION {
	  ULONG Mode;
	} FILE_MODE_INFORMATION, *PFILE_MODE_INFORMATION;

	typedef struct _FILE_ALIGNMENT_INFORMATION {
	  ULONG AlignmentRequirement;
	} FILE_ALIGNMENT_INFORMATION, *PFILE_ALIGNMENT_INFORMATION;

	typedef struct _FILE_NAME_INFORMATION {
	  ULONG FileNameLength;
	  WCHAR FileName[1];
	} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

	typedef struct _FILE_ALL_INFORMATION {
	  FILE_BASIC_INFORMATION     BasicInformation;
	  FILE_STANDARD_INFORMATION  StandardInformation;
	  FILE_INTERNAL_INFORMATION  InternalInformation;
	  FILE_EA_INFORMATION        EaInformation;
	  FILE_ACCESS_INFORMATION    AccessInformation;
	  FILE_POSITION_INFORMATION  PositionInformation;
	  FILE_MODE_INFORMATION      ModeInformation;
	  FILE_ALIGNMENT_INFORMATION AlignmentInformation;
	  FILE_NAME_INFORMATION      NameInformation;
	} FILE_ALL_INFORMATION, *PFILE_ALL_INFORMATION;

	typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION {
	  ULONG LogicalBytesPerSector;
	  ULONG PhysicalBytesPerSectorForAtomicity;
	  ULONG PhysicalBytesPerSectorForPerformance;
	  ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
	  ULONG Flags;
	  ULONG ByteOffsetForSectorAlignment;
	  ULONG ByteOffsetForPartitionAlignment;
	} FILE_FS_SECTOR_SIZE_INFORMATION, *PFILE_FS_SECTOR_SIZE_INFORMATION;

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff540310(v=vs.85).aspx
	typedef struct _FILE_ID_FULL_DIR_INFORMATION  {
	  ULONG         NextEntryOffset;
	  ULONG         FileIndex;
	  LARGE_INTEGER CreationTime;
	  LARGE_INTEGER LastAccessTime;
	  LARGE_INTEGER LastWriteTime;
	  LARGE_INTEGER ChangeTime;
	  LARGE_INTEGER EndOfFile;
	  LARGE_INTEGER AllocationSize;
	  ULONG         FileAttributes;
	  ULONG         FileNameLength;
	  ULONG         EaSize;
	  LARGE_INTEGER FileId;
	  WCHAR         FileName[1];
	} FILE_ID_FULL_DIR_INFORMATION, *PFILE_ID_FULL_DIR_INFORMATION;

	static NtQueryInformationFile_t NtQueryInformationFile;
	if(!NtQueryInformationFile)
		if(!(NtQueryInformationFile=(NtQueryInformationFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryInformationFile")))
			abort();
	static NtQueryVolumeInformationFile_t NtQueryVolumeInformationFile;
	if(!NtQueryVolumeInformationFile)
		if(!(NtQueryVolumeInformationFile=(NtQueryVolumeInformationFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryVolumeInformationFile")))
			abort();
	static NtOpenDirectoryObject_t NtOpenDirectoryObject;
	if(!NtOpenDirectoryObject)
		if(!(NtOpenDirectoryObject=(NtOpenDirectoryObject_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtOpenDirectoryObject")))
			abort();
	static NtOpenFile_t NtOpenFile;
	if(!NtOpenFile)
		if(!(NtOpenFile=(NtOpenFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtOpenFile")))
			abort();
	static NtCreateFile_t NtCreateFile;
	if(!NtCreateFile)
		if(!(NtCreateFile=(NtCreateFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtCreateFile")))
			abort();
	static NtClose_t NtClose;
	if(!NtClose)
		if(!(NtClose=(NtClose_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtClose")))
			abort();
	static RtlDosPathNameToNtPathName_U_t RtlDosPathNameToNtPathName_U;
	if(!RtlDosPathNameToNtPathName_U)
		if(!(RtlDosPathNameToNtPathName_U=(RtlDosPathNameToNtPathName_U_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "RtlDosPathNameToNtPathName_U")))
			abort();

	static NtQueryDirectoryFile_t NtQueryDirectoryFile;
	if(!NtQueryDirectoryFile)
		if(!(NtQueryDirectoryFile=(NtQueryDirectoryFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryDirectoryFile")))
			abort();

	IO_STATUS_BLOCK isb={ 0 };
	bool slowPath=(wanted.have_nlink || wanted.have_blocks || wanted.have_blksize);

	// NTFS does *not* perform well when opening handles to files with absolute paths in a directory with lots of entries
	// I'm assuming this is because NTFS is pure B-tree, so what I'm going to do is open its containing directory as
	// a separate HANDLE and keep a per-thread cache of that HANDLE so lookups in the same directory are fast.
	NTSTATUS ntval=0;
	OBJECT_ATTRIBUTES oa={sizeof(OBJECT_ATTRIBUTES)};
	struct dirhinfo_t
	{
		HANDLE h;
		UNICODE_STRING path;
		std::filesystem::path _path;
		dirhinfo_t() : h(nullptr) { memset(&path, 0, sizeof(path)); }
		~dirhinfo_t() { if(h) NtClose(h); }
	};
	static boost::thread_specific_ptr<dirhinfo_t> _dirhinfo;
	if(!_dirhinfo.get())
		_dirhinfo.reset(new dirhinfo_t);
	dirhinfo_t &dirhinfo=*_dirhinfo;
	std::filesystem::path::value_type buffer[sizeof(FILE_ALL_INFORMATION)/sizeof(std::filesystem::path::value_type)+32769];
	prefix=std::filesystem::absolute(prefix);
	if(dirhinfo._path!=prefix)
	{
		if(dirhinfo.h)
		{
			NtClose(dirhinfo.h);
			dirhinfo.h=nullptr;
		}
		dirhinfo._path=prefix;
		dirhinfo.path.Buffer=buffer;
		dirhinfo.path.Length=dirhinfo.path.MaximumLength=(USHORT) sizeof(buffer);
		RtlDosPathNameToNtPathName_U(dirhinfo._path.c_str(), &dirhinfo.path, NULL, NULL);
		oa.ObjectName=&dirhinfo.path;
		oa.Attributes=0x40/*OBJ_CASE_INSENSITIVE*/;
		//ntval|=NtOpenDirectoryObject(&dirhinfo.h, 0x1/*DIRECTORY_QUERY*/|0x2/*DIRECTORY_TRAVERSE*/, &oa);
		ntval=NtOpenFile(&dirhinfo.h, FILE_LIST_DIRECTORY|SYNCHRONIZE, &oa, &isb, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			0x01/*FILE_DIRECTORY_FILE*/|0x20/*FILE_SYNCHRONOUS_IO_NONALERT*/|0x4000/*FILE_OPEN_FOR_BACKUP_INTENT*/);
		if(0/*STATUS_SUCCESS*/!=ntval)
			return;
	}
	//slowPath=false;

	if(!slowPath)
	{
		// Fast path skips opening a handle per file by enumerating the containing directory using a glob
		// exactly matching the leafname
		UNICODE_STRING _glob;
		_glob.Buffer=const_cast<std::filesystem::path::value_type *>(leafname.c_str());
		_glob.MaximumLength=(_glob.Length=(USHORT) (leafname.native().size()*sizeof(std::filesystem::path::value_type)))+sizeof(std::filesystem::path::value_type);
		FILE_ID_FULL_DIR_INFORMATION *ffdi=(FILE_ID_FULL_DIR_INFORMATION *) buffer;
		if(0/*STATUS_SUCCESS*/!=(ntval=NtQueryDirectoryFile(dirhinfo.h, NULL, NULL, NULL, &isb, ffdi, sizeof(buffer),
			FileIdFullDirectoryInformation, TRUE, &_glob, FALSE)))
			return;
		if(wanted.have_ino) { stat.st_ino=ffdi->FileId.QuadPart; have_metadata.have_ino=1; }
		if(wanted.have_type) { stat.st_type=to_st_type(ffdi->FileAttributes); have_metadata.have_type=1; }
		if(wanted.have_atim) { stat.st_atim=to_timespec(ffdi->LastAccessTime); have_metadata.have_atim=1; }
		if(wanted.have_mtim) { stat.st_mtim=to_timespec(ffdi->LastWriteTime); have_metadata.have_mtim=1; }
		if(wanted.have_ctim) { stat.st_ctim=to_timespec(ffdi->ChangeTime); have_metadata.have_ctim=1; }
		if(wanted.have_size) { stat.st_size=ffdi->EndOfFile.QuadPart; have_metadata.have_size=1; }
		if(wanted.have_allocated) { stat.st_allocated=ffdi->AllocationSize.QuadPart; have_metadata.have_allocated=1; }
		if(wanted.have_birthtim) { stat.st_birthtim=to_timespec(ffdi->CreationTime); have_metadata.have_birthtim=1; }
	}
	else
	{
		HANDLE h=nullptr;
		UNICODE_STRING path;
		path.Buffer=const_cast<std::filesystem::path::value_type *>(leafname.c_str());
		path.MaximumLength=(path.Length=(USHORT) (leafname.native().size()*sizeof(std::filesystem::path::value_type)))+sizeof(std::filesystem::path::value_type);
		oa.ObjectName=&path;
		oa.RootDirectory=dirhinfo.h;
		oa.Attributes=0x40/*OBJ_CASE_INSENSITIVE*/; //|0x100/*OBJ_OPENLINK*/;
		ntval=NtOpenFile(&h, FILE_READ_ATTRIBUTES, &oa, &isb, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			0x040/*FILE_NON_DIRECTORY_FILE*/|0x4000/*FILE_OPEN_FOR_BACKUP_INTENT*/|0x00200000/*FILE_OPEN_REPARSE_POINT*/);
		//ntval=NtCreateFile(&h, FILE_READ_ATTRIBUTES, &oa, &isb, NULL, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
		//	0x1/*FILE_OPEN*/, 0x040/*FILE_NON_DIRECTORY_FILE*/|0x4000/*FILE_OPEN_FOR_BACKUP_INTENT*/, NULL, 0);
		if(0/*STATUS_SUCCESS*/!=ntval)
			return;
		auto undirh=detail::Undoer([&h] { NtClose(h); });
		FILE_ALL_INFORMATION &fai=*(FILE_ALL_INFORMATION *)buffer;
		FILE_FS_SECTOR_SIZE_INFORMATION ffssi={0};
		bool needInternal=(wanted.have_ino);
		bool needBasic=(wanted.have_type || wanted.have_atim || wanted.have_mtim || wanted.have_ctim || wanted.have_birthtim);
		bool needStandard=(wanted.have_nlink || wanted.have_size || wanted.have_allocated || wanted.have_blocks);
		// It's not widely known that the NT kernel supplies a stat() equivalent i.e. get me everything in a single syscall
		// However fetching FileAlignmentInformation which comes with FILE_ALL_INFORMATION is slow as it touches the device driver,
		// so only use if we need more than one item
		if((needInternal+needBasic+needStandard)>=2)
			ntval|=NtQueryInformationFile(h, &isb, &fai, sizeof(buffer), FileAllInformation);
		else
		{
			if(needInternal)
				ntval|=NtQueryInformationFile(h, &isb, &fai.InternalInformation, sizeof(fai.InternalInformation), FileInternalInformation);
			if(needBasic)
				ntval|=NtQueryInformationFile(h, &isb, &fai.BasicInformation, sizeof(fai.BasicInformation), FileBasicInformation);
			if(needStandard)
				ntval|=NtQueryInformationFile(h, &isb, &fai.StandardInformation, sizeof(fai.StandardInformation), FileStandardInformation);
		}
		if(wanted.have_blocks || wanted.have_blksize)
			ntval|=NtQueryVolumeInformationFile(h, &isb, &ffssi, sizeof(ffssi), FileFsSectorSizeInformation);
		if(0/*STATUS_SUCCESS*/!=ntval)
			return;
		if(wanted.have_ino) { stat.st_ino=fai.InternalInformation.IndexNumber.QuadPart; have_metadata.have_ino=1; }
		if(wanted.have_type) { stat.st_type=to_st_type(fai.BasicInformation.FileAttributes); have_metadata.have_type=1; }
		if(wanted.have_nlink) { stat.st_nlink=(int16_t) fai.StandardInformation.NumberOfLinks; have_metadata.have_nlink=1; }
		if(wanted.have_atim) { stat.st_atim=to_timespec(fai.BasicInformation.LastAccessTime); have_metadata.have_atim=1; }
		if(wanted.have_mtim) { stat.st_mtim=to_timespec(fai.BasicInformation.LastWriteTime); have_metadata.have_mtim=1; }
		if(wanted.have_ctim) { stat.st_ctim=to_timespec(fai.BasicInformation.ChangeTime); have_metadata.have_ctim=1; }
		if(wanted.have_size) { stat.st_size=fai.StandardInformation.EndOfFile.QuadPart; have_metadata.have_size=1; }
		if(wanted.have_allocated) { stat.st_allocated=fai.StandardInformation.AllocationSize.QuadPart; have_metadata.have_allocated=1; }
		if(wanted.have_blocks) { stat.st_blocks=fai.StandardInformation.AllocationSize.QuadPart/ffssi.PhysicalBytesPerSectorForPerformance; have_metadata.have_blocks=1; }
		if(wanted.have_blksize) { stat.st_blksize=(uint16_t) ffssi.PhysicalBytesPerSectorForPerformance; have_metadata.have_blksize=1; }
		if(wanted.have_birthtim) { stat.st_birthtim=to_timespec(fai.BasicInformation.CreationTime); have_metadata.have_birthtim=1; }
	}
#else
	struct stat s={0};
	prefix/=leafname;
	if(-1!=lstat(prefix.c_str(), &s))
	{
		if(wanted.have_dev) { stat.st_dev=s.st_dev; have_metadata.have_dev=1; }
		if(wanted.have_ino) { stat.st_ino=s.st_ino; have_metadata.have_ino=1; }
		if(wanted.have_type) { stat.st_type=s.st_mode; have_metadata.have_type=1; }
		if(wanted.have_mode) { stat.st_mode=s.st_mode; have_metadata.have_mode=1; }
		if(wanted.have_nlink) { stat.st_nlink=s.st_nlink; have_metadata.have_nlink=1; }
		if(wanted.have_uid) { stat.st_uid=s.st_uid; have_metadata.have_uid=1; }
		if(wanted.have_gid) { stat.st_gid=s.st_gid; have_metadata.have_gid=1; }
		if(wanted.have_rdev) { stat.st_rdev=s.st_rdev; have_metadata.have_rdev=1; }
		if(wanted.have_atim) { stat.st_atim.tv_sec=s.st_atim.tv_sec; stat.st_atim.tv_nsec=s.st_atim.tv_nsec; have_metadata.have_atim=1; }
		if(wanted.have_mtim) { stat.st_mtim.tv_sec=s.st_mtim.tv_sec; stat.st_mtim.tv_nsec=s.st_mtim.tv_nsec; have_metadata.have_mtim=1; }
		if(wanted.have_ctim) { stat.st_ctim.tv_sec=s.st_ctim.tv_sec; stat.st_ctim.tv_nsec=s.st_ctim.tv_nsec; have_metadata.have_ctim=1; }
		if(wanted.have_size) { stat.st_size=s.st_size; have_metadata.have_size=1; }
		if(wanted.have_allocated) { stat.st_allocated=s.st_blocks*s.st_blksize; have_metadata.have_allocated=1; }
		if(wanted.have_blocks) { stat.st_blocks=s.st_blocks; have_metadata.have_blocks=1; }
		if(wanted.have_blksize) { stat.st_blksize=s.st_blksize; have_metadata.have_blksize=1; }
#ifdef HAVE_STAT_FLAGS
		if(wanted.have_flags) { stat.st_flags=s.st_flags; have_metadata.have_flags=1; }
#endif
#ifdef HAVE_STAT_GEN
		if(wanted.have_gen) { stat.st_gen=s.st_gen; have_metadata.have_gen=1; }
#endif
#ifdef HAVE_BIRTHTIMESPEC
		if(wanted.have_birthtim) { stat.st_birthtim.tv_sec=s.st_birthtim.tv_sec; stat.st_birthtim.tv_nsec=s.st_birthtim.tv_nsec; have_metadata.have_birthtim=1; }
#endif
	}
#endif
}

void *begin_enumerate_directory(std::filesystem::path path)
{
#ifdef WIN32
	HANDLE ret=CreateFile(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	return (INVALID_HANDLE_VALUE==ret) ? 0 : ret;
#else
	int ret=open(path.c_str(), O_RDONLY | O_DIRECTORY);
	return (-1==ret) ? 0 : (void *)(size_t)ret;
#endif
}

void end_enumerate_directory(void *h)
{
#ifdef WIN32
	CloseHandle(h);
#else
	close((int)(size_t)h);
#endif
}

std::unique_ptr<std::vector<directory_entry>> enumerate_directory(void *h, size_t maxitems, std::filesystem::path glob, bool namesonly)
{
	std::unique_ptr<std::vector<directory_entry>> ret;
#ifdef WIN32
	// From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/FILE_INFORMATION_CLASS.html
	typedef enum _FILE_INFORMATION_CLASS {
		FileDirectoryInformation                 = 1,
		FileFullDirectoryInformation,
		FileBothDirectoryInformation,
		FileBasicInformation,
		FileStandardInformation,
		FileInternalInformation,
		FileEaInformation,
		FileAccessInformation,
		FileNameInformation,
		FileRenameInformation,
		FileLinkInformation,
		FileNamesInformation,
		FileDispositionInformation,
		FilePositionInformation,
		FileFullEaInformation,
		FileModeInformation,
		FileAlignmentInformation,
		FileAllInformation,
		FileAllocationInformation,
		FileEndOfFileInformation,
		FileAlternateNameInformation,
		FileStreamInformation,
		FilePipeInformation,
		FilePipeLocalInformation,
		FilePipeRemoteInformation,
		FileMailslotQueryInformation,
		FileMailslotSetInformation,
		FileCompressionInformation,
		FileObjectIdInformation,
		FileCompletionInformation,
		FileMoveClusterInformation,
		FileQuotaInformation,
		FileReparsePointInformation,
		FileNetworkOpenInformation,
		FileAttributeTagInformation,
		FileTrackingInformation,
		FileIdBothDirectoryInformation,
		FileIdFullDirectoryInformation,
		FileValidDataLengthInformation,
		FileShortNameInformation,
		FileIoCompletionNotificationInformation,
		FileIoStatusBlockRangeInformation,
		FileIoPriorityHintInformation,
		FileSfioReserveInformation,
		FileSfioVolumeInformation,
		FileHardLinkInformation,
		FileProcessIdsUsingFileInformation,
		FileNormalizedNameInformation,
		FileNetworkPhysicalNameInformation,
		FileIdGlobalTxDirectoryInformation,
		FileIsRemoteDeviceInformation,
		FileAttributeCacheInformation,
		FileNumaNodeInformation,
		FileStandardLinkInformation,
		FileRemoteProtocolInformation,
		FileMaximumInformation
	} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff540310(v=vs.85).aspx
	typedef struct _FILE_ID_FULL_DIR_INFORMATION  {
	  ULONG         NextEntryOffset;
	  ULONG         FileIndex;
	  LARGE_INTEGER CreationTime;
	  LARGE_INTEGER LastAccessTime;
	  LARGE_INTEGER LastWriteTime;
	  LARGE_INTEGER ChangeTime;
	  LARGE_INTEGER EndOfFile;
	  LARGE_INTEGER AllocationSize;
	  ULONG         FileAttributes;
	  ULONG         FileNameLength;
	  ULONG         EaSize;
	  LARGE_INTEGER FileId;
	  WCHAR         FileName[1];
	} FILE_ID_FULL_DIR_INFORMATION, *PFILE_ID_FULL_DIR_INFORMATION;

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff540329(v=vs.85).aspx
	typedef struct _FILE_NAMES_INFORMATION {
	  ULONG NextEntryOffset;
	  ULONG FileIndex;
	  ULONG FileNameLength;
	  WCHAR FileName[1];
	} FILE_NAMES_INFORMATION, *PFILE_NAMES_INFORMATION;

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif

	// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff550671(v=vs.85).aspx
	typedef struct _IO_STATUS_BLOCK {
		union {
			NTSTATUS Status;
			PVOID    Pointer;
		};
		ULONG_PTR Information;
	} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

	// From http://msdn.microsoft.com/en-us/library/windows/desktop/aa380518(v=vs.85).aspx
	typedef struct _LSA_UNICODE_STRING {
	  USHORT Length;
	  USHORT MaximumLength;
	  PWSTR  Buffer;
	} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

	// From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtQueryDirectoryFile.html
	// and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567047(v=vs.85).aspx
	typedef NTSTATUS (NTAPI *NtQueryDirectoryFile_t)(
		/*_In_*/      HANDLE FileHandle,
		/*_In_opt_*/  HANDLE Event,
		/*_In_opt_*/  void *ApcRoutine,
		/*_In_opt_*/  PVOID ApcContext,
		/*_Out_*/     PIO_STATUS_BLOCK IoStatusBlock,
		/*_Out_*/     PVOID FileInformation,
		/*_In_*/      ULONG Length,
		/*_In_*/      FILE_INFORMATION_CLASS FileInformationClass,
		/*_In_*/      BOOLEAN ReturnSingleEntry,
		/*_In_opt_*/  PUNICODE_STRING FileName,
		/*_In_*/      BOOLEAN RestartScan
		);

	static NtQueryDirectoryFile_t NtQueryDirectoryFile;
	if(!NtQueryDirectoryFile)
		if(!(NtQueryDirectoryFile=(NtQueryDirectoryFile_t) GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryDirectoryFile")))
			abort();

	IO_STATUS_BLOCK isb={ 0 };
	UNICODE_STRING _glob;
	if(!glob.empty())
	{
		_glob.Buffer=const_cast<std::filesystem::path::value_type *>(glob.c_str());
		_glob.Length=_glob.MaximumLength=(USHORT) glob.native().size();
	}

	if(namesonly)
	{
		FILE_NAMES_INFORMATION *buffer=(FILE_NAMES_INFORMATION *) malloc(sizeof(FILE_NAMES_INFORMATION)*maxitems);
		if(!buffer) throw std::bad_alloc();
		auto unbuffer=detail::Undoer([&buffer] { free(buffer); });

		if(0/*STATUS_SUCCESS*/!=NtQueryDirectoryFile(h, NULL, NULL, NULL, &isb, buffer, (ULONG)(sizeof(FILE_NAMES_INFORMATION)*maxitems),
			FileNamesInformation, FALSE, glob.empty() ? NULL : &_glob, FALSE))
		{
			return ret;
		}
		ret=std::unique_ptr<std::vector<directory_entry>>(new std::vector<directory_entry>);
		std::vector<directory_entry> &_ret=*ret;
		_ret.reserve(maxitems);
		directory_entry item;
		bool done=false;
		for(FILE_NAMES_INFORMATION *ffdi=buffer; !done; ffdi=(FILE_NAMES_INFORMATION *)((size_t) ffdi + ffdi->NextEntryOffset))
		{
			size_t length=ffdi->FileNameLength/sizeof(std::filesystem::path::value_type);
			if(length<=2 && '.'==ffdi->FileName[0])
				if(1==length || '.'==ffdi->FileName[1]) continue;
			std::filesystem::path::string_type leafname(ffdi->FileName, length);
			item.leafname=std::move(leafname);
			_ret.push_back(std::move(item));
			if(!ffdi->NextEntryOffset) done=true;
		}
	}
	else
	{
		FILE_ID_FULL_DIR_INFORMATION *buffer=(FILE_ID_FULL_DIR_INFORMATION *) malloc(sizeof(FILE_ID_FULL_DIR_INFORMATION)*maxitems);
		if(!buffer) throw std::bad_alloc();
		auto unbuffer=detail::Undoer([&buffer] { free(buffer); });

		if(0/*STATUS_SUCCESS*/!=NtQueryDirectoryFile(h, NULL, NULL, NULL, &isb, buffer, (ULONG)(sizeof(FILE_ID_FULL_DIR_INFORMATION)*maxitems),
			FileIdFullDirectoryInformation, FALSE, glob.empty() ? NULL : &_glob, FALSE))
		{
			return ret;
		}
		ret=std::unique_ptr<std::vector<directory_entry>>(new std::vector<directory_entry>);
		std::vector<directory_entry> &_ret=*ret;
		_ret.reserve(maxitems);
		directory_entry item;
		// This is what windows returns with each enumeration
		item.have_metadata.have_ino=1;
		item.have_metadata.have_type=1;
		item.have_metadata.have_atim=1;
		item.have_metadata.have_mtim=1;
		item.have_metadata.have_ctim=1;
		item.have_metadata.have_size=1;
		item.have_metadata.have_allocated=1;
		item.have_metadata.have_birthtim=1;
		bool done=false;
		for(FILE_ID_FULL_DIR_INFORMATION *ffdi=buffer; !done; ffdi=(FILE_ID_FULL_DIR_INFORMATION *)((size_t) ffdi + ffdi->NextEntryOffset))
		{
			size_t length=ffdi->FileNameLength/sizeof(std::filesystem::path::value_type);
			if(length<=2 && '.'==ffdi->FileName[0])
				if(1==length || '.'==ffdi->FileName[1]) continue;
			std::filesystem::path::string_type leafname(ffdi->FileName, length);
			item.leafname=std::move(leafname);
			item.stat.st_ino=ffdi->FileId.QuadPart;
			item.stat.st_type=to_st_type(ffdi->FileAttributes);
			item.stat.st_atim=to_timespec(ffdi->LastAccessTime);
			item.stat.st_mtim=to_timespec(ffdi->LastWriteTime);
			item.stat.st_ctim=to_timespec(ffdi->ChangeTime);
			item.stat.st_size=ffdi->EndOfFile.QuadPart;
			item.stat.st_allocated=ffdi->AllocationSize.QuadPart;
			item.stat.st_birthtim=to_timespec(ffdi->CreationTime);
			_ret.push_back(std::move(item));
			if(!ffdi->NextEntryOffset) done=true;
		}
	}
	return ret;
#else
#ifdef __linux__
	// Linux kernel defines a weird dirent with type packed at the end of d_name, so override default dirent
	struct dirent {
	   long           d_ino;
	   off_t          d_off;
	   unsigned short d_reclen;
	   char           d_name[];
	};
	// Unlike FreeBSD, Linux doesn't define a getdents() function, so we'll do that here.
	typedef int (*getdents_t)(int, char *, int);
	getdents_t getdents=(getdents_t)([](int fd, char *buf, int count) -> int { return syscall(SYS_getdents, fd, buf, count); });
#endif
	dirent *buffer=(dirent *) malloc(sizeof(dirent)*maxitems);
	if(!buffer) throw std::bad_alloc();
	auto unbuffer=detail::Undoer([&buffer] { free(buffer); });

	int bytes=getdents((int)(size_t)h, (char *) buffer, sizeof(dirent)*maxitems);
	if(bytes<=0)
		return ret;
	ret=std::unique_ptr<std::vector<directory_entry>>(new std::vector<directory_entry>);
	std::vector<directory_entry> &_ret=*ret;
	_ret.reserve(maxitems);
	directory_entry item;
	// This is what POSIX returns with getdents()
	item.have_metadata.have_ino=1;
	item.have_metadata.have_type=1;
	bool done=false;
	for(dirent *dent=buffer; !done; dent=(dirent *)((size_t) dent + dent->d_reclen))
	{
		if(!(bytes-=dent->d_reclen)) done=true;
		if(!dent->d_ino)
			continue;
		size_t length=strchr(dent->d_name, 0)-dent->d_name;
		if(length<=2 && '.'==dent->d_name[0])
			if(1==length || '.'==dent->d_name[1]) continue;
		if(!glob.empty() && fnmatch(glob.native().c_str(), dent->d_name, 0)) continue;
		std::filesystem::path::string_type leafname(dent->d_name, length);
		item.leafname=std::move(leafname);
		item.stat.st_ino=dent->d_ino;
		char d_type=
#ifdef __linux__
				*((char *) dent + dent->d_reclen - 1)
#else
				dent->d_type
#endif
				;
		if(DT_UNKNOWN==d_type)
			item.have_metadata.have_type=0;
		else
		{
			item.have_metadata.have_type=1;
			switch(d_type)
			{
			case DT_BLK:
				item.stat.st_type=S_IFBLK;
				break;
			case DT_CHR:
				item.stat.st_type=S_IFCHR;
				break;
			case DT_DIR:
				item.stat.st_type=S_IFDIR;
				break;
			case DT_FIFO:
				item.stat.st_type=S_IFIFO;
				break;
			case DT_LNK:
				item.stat.st_type=S_IFLNK;
				break;
			case DT_REG:
				item.stat.st_type=S_IFREG;
				break;
			case DT_SOCK:
				item.stat.st_type=S_IFSOCK;
				break;
			default:
				item.have_metadata.have_type=0;
				item.stat.st_type=0;
				break;
			}
		}
		_ret.push_back(std::move(item));
	}
	return ret;
#endif
}

	} // namespace afio
}//namespace boost