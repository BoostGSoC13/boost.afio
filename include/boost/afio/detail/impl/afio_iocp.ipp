/* async_file_io
Provides a threadpool and asynchronous file i/o infrastructure based on Boost.ASIO, Boost.Iostreams and filesystem
(C) 2013-2014 Niall Douglas http://www.nedprod.com/
File Created: Mar 2013
*/

#ifdef WIN32
BOOST_AFIO_V1_NAMESPACE_BEGIN
namespace detail {
    // Helper for opening files
    static inline std::pair<NTSTATUS, HANDLE> ntcreatefile(async_path_op_req req) BOOST_NOEXCEPT
    {
      DWORD access=FILE_READ_ATTRIBUTES, attribs=FILE_ATTRIBUTE_NORMAL;
      DWORD fileshare=FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
      DWORD creatdisp=0, flags=0x4000/*FILE_OPEN_FOR_BACKUP_INTENT*/|0x00200000/*FILE_OPEN_REPARSE_POINT*/;
      if(!!(req.flags & file_flags::int_opening_dir))
      {
          flags|=0x01/*FILE_DIRECTORY_FILE*/;
          access|=FILE_LIST_DIRECTORY|FILE_TRAVERSE;
          if(!!(req.flags & file_flags::Read)) access|=GENERIC_READ;
          if(!!(req.flags & file_flags::Write)) access|=GENERIC_WRITE;
          // Windows doesn't like opening directories without buffering.
          if(!!(req.flags & file_flags::OSDirect)) req.flags=req.flags & ~file_flags::OSDirect;
      }
      else
      {
          flags|=0x040/*FILE_NON_DIRECTORY_FILE*/;
          if(!!(req.flags & file_flags::Append)) access|=FILE_APPEND_DATA|SYNCHRONIZE;
          else
          {
              if(!!(req.flags & file_flags::Read)) access|=GENERIC_READ;
              if(!!(req.flags & file_flags::Write)) access|=GENERIC_WRITE;
          }
          if(!!(req.flags & file_flags::WillBeSequentiallyAccessed))
              flags|=0x00000004/*FILE_SEQUENTIAL_ONLY*/;
          else if(!!(req.flags & file_flags::WillBeRandomlyAccessed))
              flags|=0x00000800/*FILE_RANDOM_ACCESS*/;
          if(!!(req.flags & file_flags::TemporaryFile))
              attribs|=FILE_ATTRIBUTE_TEMPORARY;
          if(!!(req.flags & file_flags::DeleteOnClose) && !!(req.flags & file_flags::CreateOnlyIfNotExist))
          {
              flags|=0x00001000/*FILE_DELETE_ON_CLOSE*/;
              access|=DELETE;
          }
          if(!!(req.flags & file_flags::int_file_share_delete))
              access|=DELETE;
      }
      if(!!(req.flags & file_flags::CreateOnlyIfNotExist)) creatdisp|=0x00000002/*FILE_CREATE*/;
      else if(!!(req.flags & file_flags::Create)) creatdisp|=0x00000003/*FILE_OPEN_IF*/;
      else if(!!(req.flags & file_flags::Truncate)) creatdisp|=0x00000005/*FILE_OVERWRITE_IF*/;
      else creatdisp|=0x00000001/*FILE_OPEN*/;
      if(!!(req.flags & file_flags::OSDirect)) flags|=0x00000008/*FILE_NO_INTERMEDIATE_BUFFERING*/;
      if(!!(req.flags & file_flags::AlwaysSync)) flags|=0x00000002/*FILE_WRITE_THROUGH*/;

      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      HANDLE h=nullptr;
      IO_STATUS_BLOCK isb={ 0 };
      OBJECT_ATTRIBUTES oa={sizeof(OBJECT_ATTRIBUTES)};
      filesystem::path path(req.path.make_preferred());
      UNICODE_STRING _path;
      if(isalpha(path.native()[0]) && path.native()[1]==':')
      {
          path=ntpath_from_dospath(path);
          // If it's a DOS path, ignore case differences
          oa.Attributes=0x40/*OBJ_CASE_INSENSITIVE*/;
      }
      _path.Buffer=const_cast<filesystem::path::value_type *>(path.c_str());
      _path.MaximumLength=(_path.Length=(USHORT) (path.native().size()*sizeof(filesystem::path::value_type)))+sizeof(filesystem::path::value_type);
      oa.ObjectName=&_path;
      // Should I bother with oa.RootDirectory? For now, no.
      LARGE_INTEGER AllocationSize={0};
      return std::make_pair(NtCreateFile(&h, access, &oa, &isb, &AllocationSize, attribs, fileshare,
          creatdisp, flags, NULL, 0), h);
    }

    struct win_actual_lock_file;
    struct win_lock_file : public lock_file<win_actual_lock_file>
    {
      asio::windows::object_handle ev;
      win_lock_file(async_io_handle *_h=nullptr) : lock_file<win_actual_lock_file>(_h), ev(_h->parent()->threadsource()->io_service())
      {
        HANDLE evh;
        BOOST_AFIO_ERRHWIN(INVALID_HANDLE_VALUE!=(evh=CreateEvent(nullptr, true, false, nullptr)));
        ev.assign(evh);
      }
    };
    
    // Two modes of calling, either a handle or a leafname + dir handle
    // Overloads below call this
    static inline bool isDeletedFile(std::shared_ptr<async_io_handle> h, filesystem::path::string_type leafname, std::shared_ptr<async_io_handle> dirh)
    {
      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      HANDLE temph=h ? h->native_handle() : nullptr;
      bool isHex=true;
      if(leafname.size()==37 && !leafname.compare(0, 5, L".afio"))
      {
        // Could be one of our "deleted" files, is he ".afio" + all hex?
        for(size_t n=5; n<37; n++)
        {
          auto c=leafname[n];
          if(!((c>='1' && c<='9') || (c>='a' && c<='f')))
          {
            isHex=false;
            break;
          }
        }
      }
      if(isHex)
      {
        NTSTATUS ntstat;
        if(!temph)
        {
          // Ask to open with no rights at all, this should succeed even if perms deny any access
          std::tie(ntstat, temph)=ntcreatefile(async_path_op_req(dirh->path()/leafname, file_flags::None));
          if(ntstat)
          {
            // Couldn't open the file
            if((NTSTATUS) 0xC000000F/*STATUS_NO_SUCH_FILE*/==ntstat || (NTSTATUS) 0xC000003A/*STATUS_OBJECT_PATH_NOT_FOUND*/==ntstat)
              return true;
            else
              return false;
          }
        }
        IO_STATUS_BLOCK isb={ 0 };
        BOOST_AFIO_TYPEALIGNMENT(8) FILE_ALL_INFORMATION fai;
        ntstat=NtQueryInformationFile(temph, &isb, &fai.StandardInformation, sizeof(fai.StandardInformation), FileStandardInformation);
        if(STATUS_PENDING==ntstat)
          ntstat=NtWaitForSingleObject(temph, FALSE, NULL);
        if(!h)
          CloseHandle(temph);
        // If the query succeeded and delete is pending, filter out this entry
        if(!ntstat && fai.StandardInformation.DeletePending)
          return true;
      }
      return false;
    }
    static inline bool isDeletedFile(std::shared_ptr<async_io_handle> h) { return isDeletedFile(std::move(h), filesystem::path::string_type(), std::shared_ptr<async_io_handle>()); }
    static inline bool isDeletedFile(filesystem::path::string_type leafname, std::shared_ptr<async_io_handle> dirh) { return isDeletedFile(std::shared_ptr<async_io_handle>(), std::move(leafname), std::move(dirh)); }

    struct async_io_handle_windows : public async_io_handle
    {
        std::unique_ptr<asio::windows::random_access_handle> h;
        void *myid;
        bool has_been_added, SyncOnClose;
        void *mapaddr;
        typedef spinlock<bool> pathlock_t;
        mutable pathlock_t pathlock; filesystem::path _path;
        std::unique_ptr<win_lock_file> lockfile;

        static HANDLE int_checkHandle(HANDLE h, const filesystem::path &path)
        {
            BOOST_AFIO_ERRHWINFN(INVALID_HANDLE_VALUE!=h, path);
            return h;
        }
        async_io_handle_windows(async_file_io_dispatcher_base *_parent, std::shared_ptr<async_io_handle> _dirh, const filesystem::path &path, file_flags flags) : async_io_handle(_parent, std::move(_dirh), flags), myid(nullptr), has_been_added(false), SyncOnClose(false), mapaddr(nullptr), _path(path) { }
        inline async_io_handle_windows(async_file_io_dispatcher_base *_parent, std::shared_ptr<async_io_handle> _dirh, const filesystem::path &path, file_flags flags, bool _SyncOnClose, HANDLE _h);
        void int_close()
        {
            BOOST_AFIO_DEBUG_PRINT("D %p\n", this);
            if(mapaddr)
            {
                BOOST_AFIO_ERRHWINFN(UnmapViewOfFile(mapaddr), path());
                mapaddr=nullptr;
            }
            if(h)
            {
                // Windows doesn't provide an async fsync so do it synchronously
                if(SyncOnClose && write_count_since_fsync())
                    BOOST_AFIO_ERRHWINFN(FlushFileBuffers(myid), path());
                h->close();
                h.reset();
            }
            // Deregister AFTER close of file handle
            if(has_been_added)
            {
                parent()->int_del_io_handle(myid);
                has_been_added=false;
            }
            myid=nullptr;
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void close() override final
        {
            int_close();
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void *native_handle() const override final { return myid; }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC filesystem::path path(bool refresh=false) override final
        {
          if(refresh)
          {
            if(isDeletedFile(shared_from_this()))
            {
              lock_guard<pathlock_t> g(pathlock);
              _path.clear();
              return _path;
            }
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            HANDLE h=myid;
            IO_STATUS_BLOCK isb={ 0 };
            BOOST_AFIO_TYPEALIGNMENT(8) filesystem::path::value_type buffer[sizeof(FILE_NAME_INFORMATION)/sizeof(filesystem::path::value_type)+32769], buffer2[32769];
            buffer[0]=0;
            FILE_NAME_INFORMATION *fni=(FILE_NAME_INFORMATION *) buffer;
            NTSTATUS ntstat=NtQueryInformationFile(h, &isb, fni, sizeof(buffer), FileNameInformation);
            if(STATUS_PENDING==ntstat)
              ntstat=NtWaitForSingleObject(h, FALSE, NULL);
            BOOST_AFIO_ERRHNT(ntstat);
            UNICODE_STRING *volumepath=(UNICODE_STRING *) buffer2;
            ULONG buffer2length;
            ntstat=NtQueryObject(h, ObjectNameInformation, volumepath, sizeof(buffer2), &buffer2length);
            if(STATUS_PENDING==ntstat)
              ntstat=NtWaitForSingleObject(h, FALSE, NULL);
            BOOST_AFIO_ERRHNT(ntstat);
            lock_guard<pathlock_t> g(pathlock);
            _path=filesystem::path::string_type(volumepath->Buffer, volumepath->Length)+filesystem::path::string_type(fni->FileName, fni->FileNameLength);
            return _path;
          }
          lock_guard<pathlock_t> g(pathlock);
          return _path;
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC const filesystem::path &path() const override final
        {
          lock_guard<pathlock_t> g(pathlock);
          return _path;
        }

        // You can't use shared_from_this() in a constructor so ...
        void do_add_io_handle_to_parent()
        {
            if(myid)
            {
                parent()->int_add_io_handle(myid, shared_from_this());
                has_been_added=true;
            }
        }
        ~async_io_handle_windows()
        {
            int_close();
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC directory_entry direntry(metadata_flags wanted) const override final
        {
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            stat_t stat(nullptr);
            IO_STATUS_BLOCK isb={ 0 };
            NTSTATUS ntstat;

            HANDLE h=myid;
            BOOST_AFIO_TYPEALIGNMENT(8) filesystem::path::value_type buffer[sizeof(FILE_ALL_INFORMATION)/sizeof(filesystem::path::value_type)+32769];
            buffer[0]=0;
            FILE_ALL_INFORMATION &fai=*(FILE_ALL_INFORMATION *)buffer;
            FILE_FS_SECTOR_SIZE_INFORMATION ffssi={0};
            bool needInternal=!!(wanted&metadata_flags::ino);
            bool needBasic=(!!(wanted&metadata_flags::type) || !!(wanted&metadata_flags::atim) || !!(wanted&metadata_flags::mtim) || !!(wanted&metadata_flags::ctim) || !!(wanted&metadata_flags::birthtim) || !!(wanted&metadata_flags::sparse) || !!(wanted&metadata_flags::compressed));
            bool needStandard=true;  // needed for DeletePending
            // It's not widely known that the NT kernel supplies a stat() equivalent i.e. get me everything in a single syscall
            // However fetching FileAlignmentInformation which comes with FILE_ALL_INFORMATION is slow as it touches the device driver,
            // so only use if we need more than one item
            if(((int) needInternal+(int) needBasic+(int) needStandard)>=2)
            {
                ntstat=NtQueryInformationFile(h, &isb, &fai, sizeof(buffer), FileAllInformation);
                if(STATUS_PENDING==ntstat)
                    ntstat=NtWaitForSingleObject(h, FALSE, NULL);
                BOOST_AFIO_ERRHNTFN(ntstat, path());
            }
            else
            {
                if(needInternal)
                {
                    ntstat=NtQueryInformationFile(h, &isb, &fai.InternalInformation, sizeof(fai.InternalInformation), FileInternalInformation);
                    if(STATUS_PENDING==ntstat)
                        ntstat=NtWaitForSingleObject(h, FALSE, NULL);
                    BOOST_AFIO_ERRHNTFN(ntstat, path());
                }
                if(needBasic)
                {
                    ntstat=NtQueryInformationFile(h, &isb, &fai.BasicInformation, sizeof(fai.BasicInformation), FileBasicInformation);
                    if(STATUS_PENDING==ntstat)
                        ntstat=NtWaitForSingleObject(h, FALSE, NULL);
                    BOOST_AFIO_ERRHNTFN(ntstat, path());
                }
                if(needStandard)
                {
                    ntstat=NtQueryInformationFile(h, &isb, &fai.StandardInformation, sizeof(fai.StandardInformation), FileStandardInformation);
                    if(STATUS_PENDING==ntstat)
                        ntstat=NtWaitForSingleObject(h, FALSE, NULL);
                    BOOST_AFIO_ERRHNTFN(ntstat, path());
                }
            }
            if(!!(wanted&metadata_flags::blocks) || !!(wanted&metadata_flags::blksize))
            {
                ntstat=NtQueryVolumeInformationFile(h, &isb, &ffssi, sizeof(ffssi), FileFsSectorSizeInformation);
                if(STATUS_PENDING==ntstat)
                    ntstat=NtWaitForSingleObject(h, FALSE, NULL);
                //BOOST_AFIO_ERRHNTFN(ntstat, path());
                if(0/*STATUS_SUCCESS*/!=ntstat)
                {
                    // Windows XP and Vista don't support the FILE_FS_SECTOR_SIZE_INFORMATION
                    // API, so we'll just hardcode 512 bytes
                    ffssi.PhysicalBytesPerSectorForPerformance=512;
                }
            }
            if(!!(wanted&metadata_flags::ino)) { stat.st_ino=fai.InternalInformation.IndexNumber.QuadPart; }
            if(!!(wanted&metadata_flags::type)) { stat.st_type=windows_nt_kernel::to_st_type(fai.BasicInformation.FileAttributes); }
            if(!!(wanted&metadata_flags::nlink)) { stat.st_nlink=(int16_t) fai.StandardInformation.NumberOfLinks; }
            if(!!(wanted&metadata_flags::atim)) { stat.st_atim=to_timepoint(fai.BasicInformation.LastAccessTime); }
            if(!!(wanted&metadata_flags::mtim)) { stat.st_mtim=to_timepoint(fai.BasicInformation.LastWriteTime); }
            if(!!(wanted&metadata_flags::ctim)) { stat.st_ctim=to_timepoint(fai.BasicInformation.ChangeTime); }
            if(!!(wanted&metadata_flags::size)) { stat.st_size=fai.StandardInformation.EndOfFile.QuadPart; }
            if(!!(wanted&metadata_flags::allocated)) { stat.st_allocated=fai.StandardInformation.AllocationSize.QuadPart; }
            if(!!(wanted&metadata_flags::blocks)) { stat.st_blocks=fai.StandardInformation.AllocationSize.QuadPart/ffssi.PhysicalBytesPerSectorForPerformance; }
            if(!!(wanted&metadata_flags::blksize)) { stat.st_blksize=(uint16_t) ffssi.PhysicalBytesPerSectorForPerformance; }
            if(!!(wanted&metadata_flags::birthtim)) { stat.st_birthtim=to_timepoint(fai.BasicInformation.CreationTime); }
            if(!!(wanted&metadata_flags::sparse)) { stat.st_sparse=!!(fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE); }
            if(!!(wanted&metadata_flags::compressed)) { stat.st_compressed=!!(fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_COMPRESSED); }
            return directory_entry(fai.StandardInformation.DeletePending ? filesystem::path() : path()
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
              .leaf()
#else
              .filename()
#endif
            , stat, wanted);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC filesystem::path target() const override final
        {
            if(!opened_as_symlink())
                return filesystem::path();
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            using windows_nt_kernel::REPARSE_DATA_BUFFER;
            BOOST_AFIO_TYPEALIGNMENT(8) char buffer[sizeof(REPARSE_DATA_BUFFER)+32769];
            DWORD written=0;
            REPARSE_DATA_BUFFER *rpd=(REPARSE_DATA_BUFFER *) buffer;
            memset(rpd, 0, sizeof(*rpd));
            BOOST_AFIO_ERRHWIN(DeviceIoControl(myid, FSCTL_GET_REPARSE_POINT, NULL, 0, rpd, sizeof(buffer), &written, NULL));
            switch(rpd->ReparseTag)
            {
            case IO_REPARSE_TAG_MOUNT_POINT:
                return dospath_from_ntpath(filesystem::path::string_type(rpd->MountPointReparseBuffer.PathBuffer+rpd->MountPointReparseBuffer.SubstituteNameOffset/sizeof(filesystem::path::value_type), rpd->MountPointReparseBuffer.SubstituteNameLength/sizeof(filesystem::path::value_type)));
            case IO_REPARSE_TAG_SYMLINK:
                return dospath_from_ntpath(filesystem::path::string_type(rpd->SymbolicLinkReparseBuffer.PathBuffer+rpd->SymbolicLinkReparseBuffer.SubstituteNameOffset/sizeof(filesystem::path::value_type), rpd->SymbolicLinkReparseBuffer.SubstituteNameLength/sizeof(filesystem::path::value_type)));
            }
            BOOST_AFIO_THROW(std::runtime_error("Unknown type of symbolic link."));
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void *try_mapfile() override final
        {
            if(!mapaddr)
            {
                if(!(flags() & file_flags::Write) && !(flags() & file_flags::Append))
                {
                    HANDLE sectionh;
                    if(INVALID_HANDLE_VALUE!=(sectionh=CreateFileMapping(myid, NULL, PAGE_READONLY, 0, 0, nullptr)))
                    {
                        auto unsectionh=detail::Undoer([&sectionh]{ CloseHandle(sectionh); });
                        mapaddr=MapViewOfFile(sectionh, FILE_MAP_READ, 0, 0, 0);
                    }
                }
            }
            return mapaddr;
        }
    };
    inline async_io_handle_windows::async_io_handle_windows(async_file_io_dispatcher_base *_parent, std::shared_ptr<async_io_handle> _dirh,
      const filesystem::path &path, file_flags flags, bool _SyncOnClose, HANDLE _h)
      : async_io_handle(_parent, std::move(_dirh), flags),
        h(new asio::windows::random_access_handle(_parent->p->pool->io_service(), int_checkHandle(_h, path))), myid(_h),
        has_been_added(false), SyncOnClose(_SyncOnClose), mapaddr(nullptr), _path(path)
    {
      if(!!(flags & file_flags::OSLockable))
        lockfile=process_lockfile_registry::open<win_lock_file>(this);
    }

    class async_file_io_dispatcher_windows : public async_file_io_dispatcher_base
    {
        friend class directory_entry;
        friend void directory_entry::_int_fetch(metadata_flags wanted, std::shared_ptr<async_io_handle> dirh);

        size_t pagesize;
        // Called in unknown thread
        completion_returntype dodir(size_t id, async_io_op _, async_path_op_req req)
        {
            BOOL ret=0;
            req.flags=fileflags(req.flags)|file_flags::int_opening_dir|file_flags::Read;
            if(!(req.flags & file_flags::UniqueDirectoryHandle) && !!(req.flags & file_flags::Read) && !(req.flags & file_flags::Write))
            {
                // Return a copy of the one in the dir cache if available
                return std::make_pair(true, p->get_handle_to_dir(this, id, req, &async_file_io_dispatcher_windows::dofile));
            }
            else
            {
                // With the NT kernel, you create a directory by creating a file.
                return dofile(id, _, req);
            }
        }
        // Called in unknown thread
        completion_returntype dormdir(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            filesystem::path::string_type escapedpath(L"\\\\?\\"+req.path.native());
            BOOST_AFIO_ERRHWINFN(RemoveDirectory(escapedpath.c_str()), req.path);
            auto ret=std::make_shared<async_io_handle_windows>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags);
            return std::make_pair(true, ret);
        }
      public:
      private:
        // Called in unknown thread
        completion_returntype dofile(size_t id, async_io_op, async_path_op_req req)
        {
            std::shared_ptr<async_io_handle> dirh;
            NTSTATUS status=0;
            HANDLE h=nullptr;
            req.flags=fileflags(req.flags);
            if(!!(req.flags & file_flags::FastDirectoryEnumeration))
                dirh=p->get_handle_to_containing_dir(this, id, req, &async_file_io_dispatcher_windows::dofile);
            std::tie(status, h)=ntcreatefile(req);
            BOOST_AFIO_ERRHNTFN(status, req.path);
            // If writing and SyncOnClose and NOT synchronous, turn on SyncOnClose
            auto ret=std::make_shared<async_io_handle_windows>(this, dirh, req.path, req.flags,
                (file_flags::SyncOnClose|file_flags::Write)==(req.flags & (file_flags::SyncOnClose|file_flags::Write|file_flags::AlwaysSync)),
                h);
            static_cast<async_io_handle_windows *>(ret.get())->do_add_io_handle_to_parent();
            // If creating a file or directory or link and he wants compression, try that now
            if(!!(req.flags & file_flags::CreateCompressed) && (!!(req.flags & file_flags::CreateOnlyIfNotExist) || !!(req.flags & file_flags::Create)))
            {
              DWORD bytesout=0;
              USHORT val=COMPRESSION_FORMAT_DEFAULT;
              BOOST_AFIO_ERRHWINFN(DeviceIoControl(ret->native_handle(), FSCTL_SET_COMPRESSION, &val, sizeof(val), nullptr, 0, &bytesout, nullptr), req.path);
            }
            if(!(req.flags & file_flags::int_opening_dir) && !(req.flags & file_flags::int_opening_link))
            {
              // If opening existing file for write, try to convert to sparse, ignoring any failures
              if(!(req.flags & file_flags::NoSparse) && !!(req.flags & file_flags::Write))
              {
#if defined(__MINGW32__) && !defined(__MINGW64__) && !defined(__MINGW64_VERSION_MAJOR)
                // Mingw32 currently lacks the FILE_SET_SPARSE_BUFFER structure
                typedef struct _FILE_SET_SPARSE_BUFFER {
                  BOOLEAN SetSparse;
                } FILE_SET_SPARSE_BUFFER, *PFILE_SET_SPARSE_BUFFER;
#endif
                DWORD bytesout=0;
                FILE_SET_SPARSE_BUFFER fssb={true};
                DeviceIoControl(ret->native_handle(), FSCTL_SET_SPARSE, &fssb, sizeof(fssb), nullptr, 0, &bytesout, nullptr);
              }
              if(!!(req.flags & file_flags::OSMMap))
                ret->try_mapfile();
            }
            return std::make_pair(true, ret);
        }
        // Generates a 32 character long crypto strong random name
        static filesystem::path::string_type make_randomname()
        {
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            static const filesystem::path::string_type::value_type table[]=L"0123456789abcdef";
            filesystem::path::string_type ret;
            char buffer[16];
            BOOST_AFIO_ERRHWIN(RtlGenRandom(buffer, sizeof(buffer))); // crypto secure
            ret.reserve(2*sizeof(buffer));
            for(size_t n=0; n<sizeof(buffer); n++)
            {
              ret.push_back(table[buffer[n]&0xf]);
              ret.push_back(table[(buffer[n]>>4)&0xf]);
            }
            return ret;
        }
        // Called in unknown thread
        completion_returntype dormfile(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            // To emulate POSIX unlink semantics, we first rename the file to something random
            // before we delete it.
            filesystem::path randompath(req.path.parent_path()/(L".afio"+make_randomname()));
            filesystem::path::string_type escapedpath(L"\\\\?\\"+req.path.native()), escapedrandompath(L"\\\\?\\"+randompath.native());
            if(MoveFile(escapedpath.c_str(), escapedrandompath.c_str()))
            {
              // TODO: Also mark with hidden attributes? If you do, instead of MoveFile open the file and use rename + attribs on it.
              BOOST_AFIO_ERRHWINFN(DeleteFile(escapedrandompath.c_str()), req.path);
            }
            else
            {
              BOOST_AFIO_ERRHWINFN(DeleteFile(escapedpath.c_str()), req.path);
            }
            auto ret=std::make_shared<async_io_handle_windows>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags);
            return std::make_pair(true, ret);
        }
        // Called in unknown thread
        void boost_asio_symlink_completion_handler(size_t id, std::shared_ptr<async_io_handle> h, std::shared_ptr<std::unique_ptr<filesystem::path::value_type[]>> buffer, const asio::error_code &ec, size_t bytes_transferred)
        {
            if(ec)
            {
                exception_ptr e;
                // boost::system::system_error makes no attempt to ask windows for what the error code means :(
                try
                {
                    BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
                }
                catch(...)
                {
                    e=current_exception();
                }
                complete_async_op(id, h, e);
            }
            else
            {
                complete_async_op(id, h);
            }
        }
        // Called in unknown thread
        completion_returntype dosymlink(size_t id, async_io_op op, async_path_op_req req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            req.flags=fileflags(req.flags);
            req.flags=req.flags|file_flags::int_opening_link;
            // For safety, best not create unless doesn't exist
            if(!!(req.flags&file_flags::Create))
            {
                req.flags=req.flags&~file_flags::Create;
                req.flags=req.flags|file_flags::CreateOnlyIfNotExist;
            }
            // If not creating, simply open
            if(!(req.flags&file_flags::CreateOnlyIfNotExist))
            {
                return dodir(id, op, req);
            }
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            using windows_nt_kernel::REPARSE_DATA_BUFFER;
            // Our new object needs write access and if we're linking a directory, create a directory
            req.flags=req.flags|file_flags::Write;
            if(!!(h->flags()&file_flags::int_opening_dir))
              req.flags=req.flags|file_flags::int_opening_dir;
            completion_returntype ret=dodir(id, op, req);
            assert(ret.first);
            filesystem::path destpath(h->path());
            size_t destpathbytes=destpath.native().size()*sizeof(filesystem::path::value_type);
            size_t buffersize=sizeof(REPARSE_DATA_BUFFER)+destpathbytes*2+256;
            auto buffer=std::make_shared<std::unique_ptr<filesystem::path::value_type[]>>(new filesystem::path::value_type[buffersize]);
            REPARSE_DATA_BUFFER *rpd=(REPARSE_DATA_BUFFER *) buffer->get();
            memset(rpd, 0, sizeof(*rpd));
            // Create a directory junction for directories and symbolic links for files
            if(!!(h->flags()&file_flags::int_opening_dir))
              rpd->ReparseTag=IO_REPARSE_TAG_MOUNT_POINT;
            else
              rpd->ReparseTag=IO_REPARSE_TAG_SYMLINK;
            if(isalpha(destpath.native()[0]) && destpath.native()[1]==':')
            {
                destpath=ntpath_from_dospath(destpath);
                destpathbytes=destpath.native().size()*sizeof(filesystem::path::value_type);
                memcpy(rpd->MountPointReparseBuffer.PathBuffer, destpath.c_str(), destpathbytes+sizeof(filesystem::path::value_type));
                rpd->MountPointReparseBuffer.SubstituteNameOffset=0;
                rpd->MountPointReparseBuffer.SubstituteNameLength=(USHORT)destpathbytes;
                rpd->MountPointReparseBuffer.PrintNameOffset=(USHORT)(destpathbytes+sizeof(filesystem::path::value_type));
                rpd->MountPointReparseBuffer.PrintNameLength=(USHORT)(h->path().native().size()*sizeof(filesystem::path::value_type));
                memcpy(rpd->MountPointReparseBuffer.PathBuffer+rpd->MountPointReparseBuffer.PrintNameOffset/sizeof(filesystem::path::value_type), h->path().c_str(), rpd->MountPointReparseBuffer.PrintNameLength+sizeof(filesystem::path::value_type));
            }
            else
            {
                memcpy(rpd->MountPointReparseBuffer.PathBuffer, destpath.c_str(), destpathbytes+sizeof(filesystem::path::value_type));
                rpd->MountPointReparseBuffer.SubstituteNameOffset=0;
                rpd->MountPointReparseBuffer.SubstituteNameLength=(USHORT)destpathbytes;
                rpd->MountPointReparseBuffer.PrintNameOffset=(USHORT)(destpathbytes+sizeof(filesystem::path::value_type));
                rpd->MountPointReparseBuffer.PrintNameLength=(USHORT)destpathbytes;
                memcpy(rpd->MountPointReparseBuffer.PathBuffer+rpd->MountPointReparseBuffer.PrintNameOffset/sizeof(filesystem::path::value_type), h->path().c_str(), rpd->MountPointReparseBuffer.PrintNameLength+sizeof(filesystem::path::value_type));
            }
            size_t headerlen=offsetof(REPARSE_DATA_BUFFER, MountPointReparseBuffer);
            size_t reparsebufferheaderlen=offsetof(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer)-headerlen;
            rpd->ReparseDataLength=(USHORT)(rpd->MountPointReparseBuffer.SubstituteNameLength+rpd->MountPointReparseBuffer.PrintNameLength+2*sizeof(filesystem::path::value_type)+reparsebufferheaderlen);
            auto dirop(ret.second);
            asio::windows::overlapped_ptr ol(threadsource()->io_service(), [this, id,
              BOOST_AFIO_LAMBDA_MOVE_CAPTURE(dirop),
              BOOST_AFIO_LAMBDA_MOVE_CAPTURE(buffer)](const asio::error_code &ec, size_t bytes){ boost_asio_symlink_completion_handler(id, std::move(dirop), std::move(buffer), ec, bytes);});
            DWORD bytesout=0;
            BOOL ok=DeviceIoControl(ret.second->native_handle(), FSCTL_SET_REPARSE_POINT, rpd, (DWORD)(rpd->ReparseDataLength+headerlen), NULL, 0, &bytesout, ol.get());
            DWORD errcode=GetLastError();
            if(!ok && ERROR_IO_PENDING!=errcode)
            {
                //std::cerr << "ERROR " << errcode << std::endl;
                asio::error_code ec(errcode, asio::error::get_system_category());
                ol.complete(ec, ol.get()->InternalHigh);
            }
            else
                ol.release();
            // Indicate we're not finished yet
            return std::make_pair(false, h);
        }
        // Called in unknown thread
        completion_returntype dormsymlink(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            filesystem::path::string_type escapedpath(L"\\\\?\\"+req.path.native());
            if(GetFileAttributes(escapedpath.c_str())&FILE_ATTRIBUTE_DIRECTORY)
            {
              BOOST_AFIO_ERRHWINFN(RemoveDirectory(escapedpath.c_str()), req.path);
            }
            else
            {
              BOOST_AFIO_ERRHWINFN(DeleteFile(escapedpath.c_str()), req.path);
            }
            auto ret=std::make_shared<async_io_handle_windows>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags);
            return std::make_pair(true, ret);
        }
        // Called in unknown thread
        completion_returntype dozero(size_t id, async_io_op op, std::vector<std::pair<off_t, off_t>> ranges)
        {
#if defined(__MINGW32__) && !defined(__MINGW64__) && !defined(__MINGW64_VERSION_MAJOR)
            // Mingw32 currently lacks the FILE_ZERO_DATA_INFORMATION structure and FSCTL_SET_ZERO_DATA
            typedef struct _FILE_ZERO_DATA_INFORMATION {
              LARGE_INTEGER FileOffset;
              LARGE_INTEGER BeyondFinalZero;
            } FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;
#define FSCTL_SET_ZERO_DATA             CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 50, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            auto buffers=std::make_shared<std::vector<FILE_ZERO_DATA_INFORMATION>>();
            buffers->reserve(ranges.size());
            off_t bytes=0;
            for(auto &i : ranges)
            {
              FILE_ZERO_DATA_INFORMATION fzdi;
              fzdi.FileOffset.QuadPart=i.first;
              fzdi.BeyondFinalZero.QuadPart=i.first+i.second;
              buffers->push_back(std::move(fzdi));
              bytes+=i.second;
            }
            auto bytes_to_transfer=std::make_shared<std::pair<atomic<bool>, atomic<off_t>>>();
            bytes_to_transfer->first=false;
            bytes_to_transfer->second=bytes;
            auto completion_handler=[this, id, h, bytes_to_transfer, buffers](const asio::error_code &ec, size_t bytes, off_t thisbytes)
            {
              if(ec)
              {
                exception_ptr e;
                // boost::system::system_error makes no attempt to ask windows for what the error code means :(
                try
                {
                  BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
                }
                catch(...)
                {
                  e=current_exception();
                  bool exp=false;
                  // If someone else has already returned an error, simply exit
                  if(!bytes_to_transfer->first.compare_exchange_strong(exp, true))
                    return;
                }
                complete_async_op(id, h, e);
              }
              else if(!(bytes_to_transfer->second-=thisbytes))
              {
                complete_async_op(id, h);
              }
            };
            for(auto &i : *buffers)
            {
              asio::windows::overlapped_ptr ol(threadsource()->io_service(), std::bind(completion_handler, std::placeholders::_1, std::placeholders::_2, i.BeyondFinalZero.QuadPart-i.FileOffset.QuadPart));
              DWORD bytesout=0;
              BOOL ok=DeviceIoControl(p->native_handle(), FSCTL_SET_ZERO_DATA, &i, sizeof(i), nullptr, 0, &bytesout, ol.get());
              DWORD errcode=GetLastError();
              if(!ok && ERROR_IO_PENDING!=errcode)
              {
                //std::cerr << "ERROR " << errcode << std::endl;
                asio::error_code ec(errcode, asio::error::get_system_category());
                ol.complete(ec, ol.get()->InternalHigh);
              }
              else
                ol.release();
            }
            // Indicate we're not finished yet
            return std::make_pair(false, h);
        }
        // Called in unknown thread
        completion_returntype dosync(size_t id, async_io_op op, async_io_op)
        {
          std::shared_ptr<async_io_handle> h(op.get());
          async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
          assert(p);
          off_t bytestobesynced=p->write_count_since_fsync();
          if(bytestobesynced)
            BOOST_AFIO_ERRHWINFN(FlushFileBuffers(p->native_handle()), p->path());
          p->byteswrittenatlastfsync+=bytestobesynced;
          return std::make_pair(true, h);
        }
        // Called in unknown thread
        completion_returntype doclose(size_t id, async_io_op op, async_io_op)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            if(!!(p->flags() & file_flags::int_opening_dir) && !(p->flags() & file_flags::UniqueDirectoryHandle) && !!(p->flags() & file_flags::Read) && !(p->flags() & file_flags::Write))
            {
                // As this is a directory which may be a fast directory enumerator, ignore close
            }
            else
            {
                p->close();
            }
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        void boost_asio_readwrite_completion_handler(bool is_write, size_t id, std::shared_ptr<async_io_handle> h, std::shared_ptr<std::tuple<atomic<bool>, atomic<size_t>, detail::async_data_op_req_impl<true>>> bytes_to_transfer, std::tuple<off_t, size_t, size_t, size_t> pars, const asio::error_code &ec, size_t bytes_transferred)
        {
            if(!this->p->filters_buffers.empty())
            {
                for(auto &i: this->p->filters_buffers)
                {
                    if(i.first==OpType::Unknown || (!is_write && i.first==OpType::read) || (is_write && i.first==OpType::write))
                    {
                        i.second(is_write ? OpType::write : OpType::read, h.get(), std::get<2>(*bytes_to_transfer), std::get<0>(pars), std::get<1>(pars), std::get<2>(pars), ec, bytes_transferred);
                    }
                }
            }
            if(ec)
            {
                exception_ptr e;
                // boost::system::system_error makes no attempt to ask windows for what the error code means :(
                try
                {
                    BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
                }
                catch(...)
                {
                    e=current_exception();
                    bool exp=false;
                    // If someone else has already returned an error, simply exit
                    if(!std::get<0>(*bytes_to_transfer).compare_exchange_strong(exp, true))
                        return;
                }
                complete_async_op(id, h, e);
            }
            else
            {
                async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
                if(is_write)
                    p->byteswritten+=bytes_transferred;
                else
                    p->bytesread+=bytes_transferred;
                size_t togo=(std::get<1>(*bytes_to_transfer)-=std::get<3>(pars));
                if(!togo) // bytes_this_chunk may not equal bytes_transferred if final 4Kb chunk of direct file
                    complete_async_op(id, h);
                if(togo>((size_t)1<<(8*sizeof(size_t)-1)))
                    BOOST_AFIO_THROW_FATAL(std::runtime_error("IOCP returned more bytes than we asked for. This is probably memory corruption."));
                BOOST_AFIO_DEBUG_PRINT("H %u e=%u togo=%u bt=%u bc=%u\n", (unsigned) id, (unsigned) ec.value(), (unsigned) togo, (unsigned) bytes_transferred, (unsigned) std::get<3>(pars));
            }
            //std::cout << "id=" << id << " total=" << bytes_to_transfer->second << " this=" << bytes_transferred << std::endl;
        }
        template<bool iswrite> void doreadwrite(size_t id, std::shared_ptr<async_io_handle> h, detail::async_data_op_req_impl<iswrite> req, async_io_handle_windows *p)
        {
            // asio::async_read_at() seems to have a bug and only transfers 64Kb per buffer
            // asio::windows::random_access_handle::async_read_some_at() clearly bothers
            // with the first buffer only. Same goes for both async write functions.
            //
            // So we implement by hand and skip ASIO altogether.
            size_t amount=0;
            for(auto &b: req.buffers)
            {
                amount+=asio::buffer_size(b);
            }
            auto bytes_to_transfer=std::make_shared<std::tuple<atomic<bool>, atomic<size_t>, detail::async_data_op_req_impl<true>>>();
            //mingw choked on atomic<T>::operator=, thought amount was atomic&, so changed to store to avoid issue
            std::get<1>(*bytes_to_transfer).store(amount);
            std::get<2>(*bytes_to_transfer)=req;
            // Are we using direct i/o, because then we get the magic scatter/gather special functions?
            if(!!(p->flags() & file_flags::OSDirect))
            {
                // Yay we can use the direct i/o scatter gather functions which are far more efficient
                size_t pages=amount/pagesize, thisbufferoffset;
                std::vector<FILE_SEGMENT_ELEMENT> elems(1+pages);
                auto bufferit=req.buffers.begin();
                thisbufferoffset=0;
                for(size_t n=0; n<pages; n++)
                {
                    // Be careful of 32 bit void * sign extension here ...
                    elems[n].Alignment=((size_t) asio::buffer_cast<const void *>(*bufferit))+thisbufferoffset;
                    thisbufferoffset+=pagesize;
                    if(thisbufferoffset>=asio::buffer_size(*bufferit))
                    {
                        ++bufferit;
                        thisbufferoffset=0;
                    }
                }
                elems[pages].Alignment=0;
                auto t(std::make_tuple(req.where, 0, req.buffers.size(), amount));
                asio::windows::overlapped_ptr ol(threadsource()->io_service(), [this, id, h, bytes_to_transfer,
                  BOOST_AFIO_LAMBDA_MOVE_CAPTURE(t)](const asio::error_code &ec, size_t bytes) {
                    boost_asio_readwrite_completion_handler(iswrite, id, std::move(h),
                      bytes_to_transfer, std::move(t), ec, bytes); });
                ol.get()->Offset=(DWORD) (req.where & 0xffffffff);
                ol.get()->OffsetHigh=(DWORD) ((req.where>>32) & 0xffffffff);
                BOOL ok=iswrite ? WriteFileGather
                    (p->native_handle(), &elems.front(), (DWORD) amount, NULL, ol.get())
                    : ReadFileScatter
                    (p->native_handle(), &elems.front(), (DWORD) amount, NULL, ol.get());
                DWORD errcode=GetLastError();
                if(!ok && ERROR_IO_PENDING!=errcode)
                {
                    //std::cerr << "ERROR " << errcode << std::endl;
                    asio::error_code ec(errcode, asio::error::get_system_category());
                    ol.complete(ec, ol.get()->InternalHigh);
                }
                else
                    ol.release();
            }
            else
            {
                size_t offset=0, n=0;
                for(auto &b: req.buffers)
                {
                    auto t(std::make_tuple(req.where+offset, n, 1, asio::buffer_size(b)));
                    asio::windows::overlapped_ptr ol(threadsource()->io_service(),  [this, id, h, bytes_to_transfer,
                      BOOST_AFIO_LAMBDA_MOVE_CAPTURE(t)](const asio::error_code &ec, size_t bytes) {
                        boost_asio_readwrite_completion_handler(iswrite, id, std::move(h),
                        bytes_to_transfer, std::move(t), ec, bytes); });
                    ol.get()->Offset=(DWORD) ((req.where+offset) & 0xffffffff);
                    ol.get()->OffsetHigh=(DWORD) (((req.where+offset)>>32) & 0xffffffff);
                    DWORD bytesmoved=0;
                    BOOL ok=iswrite ? WriteFile
                        (p->native_handle(), asio::buffer_cast<const void *>(b), (DWORD) asio::buffer_size(b), &bytesmoved, ol.get())
                        : ReadFile
                        (p->native_handle(), (LPVOID) asio::buffer_cast<const void *>(b), (DWORD) asio::buffer_size(b), &bytesmoved, ol.get());
                    DWORD errcode=GetLastError();
                    if(!ok && ERROR_IO_PENDING!=errcode)
                    {
                        //std::cerr << "ERROR " << errcode << std::endl;
                        asio::error_code ec(errcode, asio::error::get_system_category());
                        ol.complete(ec, ol.get()->InternalHigh);
                    }
                    else
                        ol.release();
                    offset+=asio::buffer_size(b);
                    n++;
                }
            }
        }
        // Called in unknown thread
        completion_returntype doread(size_t id, async_io_op op, detail::async_data_op_req_impl<false> req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            BOOST_AFIO_DEBUG_PRINT("R %u %p (%c) @ %u, b=%u\n", (unsigned) id, h.get(), p->path().native().back(), (unsigned) req.where, (unsigned) req.buffers.size());
#ifdef DEBUG_PRINTING
            for(auto &b: req.buffers)
            {   BOOST_AFIO_DEBUG_PRINT("  R %u: %p %u\n", (unsigned) id, asio::buffer_cast<const void *>(b), (unsigned) asio::buffer_size(b)); }
#endif
            if(p->mapaddr)
            {
                void *addr=(void *)((char *) p->mapaddr + req.where);
                for(auto &b: req.buffers)
                {
                    memcpy(asio::buffer_cast<void *>(b), addr, asio::buffer_size(b));
                    addr=(void *)((char *) addr + asio::buffer_size(b));
                }
                return std::make_pair(true, h);
            }
            else
            {
                doreadwrite(id, h, req, p);
                // Indicate we're not finished yet
                return std::make_pair(false, h);
            }
        }
        // Called in unknown thread
        completion_returntype dowrite(size_t id, async_io_op op, detail::async_data_op_req_impl<true> req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            BOOST_AFIO_DEBUG_PRINT("W %u %p (%c) @ %u, b=%u\n", (unsigned) id, h.get(), p->path().native().back(), (unsigned) req.where, (unsigned) req.buffers.size());
#ifdef DEBUG_PRINTING
            for(auto &b: req.buffers)
            {   BOOST_AFIO_DEBUG_PRINT("  W %u: %p %u\n", (unsigned) id, asio::buffer_cast<const void *>(b), (unsigned) asio::buffer_size(b)); }
#endif
            doreadwrite(id, h, req, p);
            // Indicate we're not finished yet
            return std::make_pair(false, h);
        }
        // Called in unknown thread
        completion_returntype dotruncate(size_t id, async_io_op op, off_t _newsize)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            BOOST_AFIO_DEBUG_PRINT("T %u %p (%c)\n", (unsigned) id, h.get(), p->path().native().back());
#if 1
            BOOST_AFIO_ERRHWINFN(wintruncate(p->native_handle(), _newsize), p->path());
#else
            // This is a bit tricky ... overlapped files ignore their file position except in this one
            // case, but clearly here we have a race condition. No choice but to rinse and repeat I guess.
            LARGE_INTEGER size={0}, newsize={0};
            newsize.QuadPart=_newsize;
            while(size.QuadPart!=newsize.QuadPart)
            {
                BOOST_AFIO_ERRHWINFN(SetFilePointerEx(p->native_handle(), newsize, NULL, FILE_BEGIN), p->path());
                BOOST_AFIO_ERRHWINFN(SetEndOfFile(p->native_handle()), p->path());
                BOOST_AFIO_ERRHWINFN(GetFileSizeEx(p->native_handle(), &size), p->path());
            }
#endif
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        struct enumerate_state : std::enable_shared_from_this<enumerate_state>
        {
            size_t id;
            async_io_op op;
            std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> ret;
            std::unique_ptr<windows_nt_kernel::FILE_ID_FULL_DIR_INFORMATION[]> buffer;
            async_enumerate_op_req req;
            enumerate_state(size_t _id, async_io_op _op, std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> _ret,
              async_enumerate_op_req _req) : id(_id), op(std::move(_op)), ret(std::move(_ret)), req(std::move(_req)) { reallocate_buffer(); }
            void reallocate_buffer()
            {
              using windows_nt_kernel::FILE_ID_FULL_DIR_INFORMATION;
              buffer=std::unique_ptr<FILE_ID_FULL_DIR_INFORMATION[]>(new FILE_ID_FULL_DIR_INFORMATION[req.maxitems]);
            }
        };
        typedef std::shared_ptr<enumerate_state> enumerate_state_t;
        void boost_asio_enumerate_completion_handler(enumerate_state_t state, const asio::error_code &ec, size_t bytes_transferred)
        {
            using namespace windows_nt_kernel;
            size_t id=state->id;
            std::shared_ptr<async_io_handle> h(state->op.get());
            std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> &ret=state->ret;
            std::unique_ptr<FILE_ID_FULL_DIR_INFORMATION[]> &buffer=state->buffer;
            async_enumerate_op_req &req=state->req;
            if(ec && ERROR_MORE_DATA==ec.value() && bytes_transferred<(sizeof(FILE_ID_FULL_DIR_INFORMATION)+buffer.get()->FileNameLength))
            {
                // Bump maxitems by one and reschedule.
                req.maxitems++;
                state->reallocate_buffer();
                doenumerate(state);
                return;
            }
            if(ec && ERROR_MORE_DATA!=ec.value())
            {
                if(ERROR_NO_MORE_FILES==ec.value())
                {
                    ret->set_value(std::make_pair(std::vector<directory_entry>(), false));
                    complete_async_op(id, h);
                    return;
                }
                exception_ptr e;
                // boost::system::system_error makes no attempt to ask windows for what the error code means :(
                try
                {
                    BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
                }
                catch(...)
                {
                    e=current_exception();
                }
                ret->set_exception(e);
                complete_async_op(id, h, e);
            }
            else
            {
                async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
                using windows_nt_kernel::to_st_type;
                using windows_nt_kernel::to_timepoint;
                std::vector<directory_entry> _ret;
                _ret.reserve(req.maxitems);
                bool thisbatchdone=(sizeof(FILE_ID_FULL_DIR_INFORMATION)*req.maxitems-bytes_transferred)>sizeof(FILE_ID_FULL_DIR_INFORMATION);
                directory_entry item;
                // This is what windows returns with each enumeration
                item.have_metadata=directory_entry::metadata_fastpath();
                bool needmoremetadata=!!(req.metadata&~item.have_metadata);
                bool done=false;
                for(FILE_ID_FULL_DIR_INFORMATION *ffdi=buffer.get(); !done; ffdi=(FILE_ID_FULL_DIR_INFORMATION *)((size_t) ffdi + ffdi->NextEntryOffset))
                {
                    if(!ffdi->NextEntryOffset) done=true;
                    size_t length=ffdi->FileNameLength/sizeof(filesystem::path::value_type);
                    if(length<=2 && '.'==ffdi->FileName[0])
                        if(1==length || '.'==ffdi->FileName[1])
                            continue;
                    filesystem::path::string_type leafname(ffdi->FileName, length);
                    if(isDeletedFile(leafname, h))
                      continue;
                    item.leafname=std::move(leafname);
                    item.stat.st_ino=ffdi->FileId.QuadPart;
                    item.stat.st_type=to_st_type(ffdi->FileAttributes);
                    item.stat.st_atim=to_timepoint(ffdi->LastAccessTime);
                    item.stat.st_mtim=to_timepoint(ffdi->LastWriteTime);
                    item.stat.st_ctim=to_timepoint(ffdi->ChangeTime);
                    item.stat.st_size=ffdi->EndOfFile.QuadPart;
                    item.stat.st_allocated=ffdi->AllocationSize.QuadPart;
                    item.stat.st_birthtim=to_timepoint(ffdi->CreationTime);
                    item.stat.st_sparse=!!(ffdi->FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE);
                    item.stat.st_compressed=!!(ffdi->FileAttributes & FILE_ATTRIBUTE_COMPRESSED);
                    _ret.push_back(std::move(item));
                }
                if(needmoremetadata)
                {
                    for(auto &i: _ret)
                    {
                        i.fetch_metadata(h, req.metadata);
                    }
                }
                ret->set_value(std::make_pair(std::move(_ret), !thisbatchdone));
                complete_async_op(id, h);
            }
        }
        // Called in unknown thread
        void doenumerate(enumerate_state_t state)
        {
            size_t id=state->id;
            std::shared_ptr<async_io_handle> h(state->op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            using namespace windows_nt_kernel;
            std::unique_ptr<FILE_ID_FULL_DIR_INFORMATION[]> &buffer=state->buffer;
            async_enumerate_op_req &req=state->req;
            NTSTATUS ntstat;
            UNICODE_STRING _glob={ 0 };
            if(!req.glob.empty())
            {
                _glob.Buffer=const_cast<filesystem::path::value_type *>(req.glob.c_str());
                _glob.MaximumLength=(_glob.Length=(USHORT) (req.glob.native().size()*sizeof(filesystem::path::value_type)))+sizeof(filesystem::path::value_type);
            }
            asio::windows::overlapped_ptr ol(threadsource()->io_service(), [this, state](const asio::error_code &ec, size_t bytes)
            {
              if(!state)
                abort();
              boost_asio_enumerate_completion_handler(state, ec, bytes);
            });
            bool done;
            do
            {
                // 2015-01-03 ned: I've been battling this stupid memory corruption bug for a while now, and it's really a bug in NT because they're probably not
                //                 testing asynchronous direction enumeration so hence this evil workaround. Basically 32 bit kernels will corrupt memory if
                //                 ApcContext is the i/o status block, and they demand ApcContext to be null or else! Unfortunately 64 bit kernels, including
                //                 when a 32 bit process is running under WoW64, pass ApcContext not IoStatusBlock as the completion port overlapped output,
                //                 so on 64 bit kernels we have no choice and must always set ApcContext to IoStatusBlock!
                //
                //                 So, if I'm a 32 bit build, check IsWow64Process() to see if I'm really 32 bit and don't set ApcContext, else set ApcContext.
                void *ApcContext=ol.get();
#ifndef _WIN64
                BOOL isWow64=false;
                if(IsWow64Process(GetCurrentProcess(), &isWow64), !isWow64)
                  ApcContext=nullptr;
#endif
                ntstat=NtQueryDirectoryFile(p->native_handle(), ol.get()->hEvent, NULL, ApcContext, (PIO_STATUS_BLOCK) ol.get(),
                    buffer.get(), (ULONG)(sizeof(FILE_ID_FULL_DIR_INFORMATION)*req.maxitems),
                    FileIdFullDirectoryInformation, FALSE, req.glob.empty() ? NULL : &_glob, req.restart);
                if(STATUS_BUFFER_OVERFLOW==ntstat)
                {
                    req.maxitems++;
                    state->reallocate_buffer();
                    done=false;
                }
                else done=true;
            } while(!done);
            if(STATUS_PENDING!=ntstat)
            {
                //std::cerr << "ERROR " << errcode << std::endl;
                SetWin32LastErrorFromNtStatus(ntstat);
                asio::error_code ec(GetLastError(), asio::error::get_system_category());
                ol.complete(ec, ol.get()->InternalHigh);
            }
            else
                ol.release();
        }
        // Called in unknown thread
        completion_returntype doenumerate(size_t id, async_io_op op, async_enumerate_op_req req, std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> ret)
        {
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;

            // A bit messy this, but necessary
            enumerate_state_t state=std::make_shared<enumerate_state>(
                id,
                std::move(op),
                std::move(ret),
                std::move(req)
                );
            doenumerate(std::move(state));

            // Indicate we're not finished yet
            return std::make_pair(false, std::shared_ptr<async_io_handle>());
        }
        // Called in unknown thread
        completion_returntype doextents(size_t id, async_io_op op, std::shared_ptr<promise<std::vector<std::pair<off_t, off_t>>>> ret, size_t entries)
        {
#if defined(__MINGW32__) && !defined(__MINGW64__) && !defined(__MINGW64_VERSION_MAJOR)
            // Mingw32 currently lacks the FILE_ALLOCATED_RANGE_BUFFER structure and FSCTL_QUERY_ALLOCATED_RANGES
            typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
              LARGE_INTEGER FileOffset;
              LARGE_INTEGER Length;
            } FILE_ALLOCATED_RANGE_BUFFER, *PFILE_ALLOCATED_RANGE_BUFFER;
#define FSCTL_QUERY_ALLOCATED_RANGES    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 51,  METHOD_NEITHER, FILE_READ_DATA)
#endif
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            auto buffers=std::make_shared<std::vector<FILE_ALLOCATED_RANGE_BUFFER>>(entries);
            auto completion_handler=[this, id, op, ret, entries, buffers](const asio::error_code &ec, size_t bytes)
            {
              std::shared_ptr<async_io_handle> h(op.get());
              if(ec)
              {
                if(ERROR_MORE_DATA==ec.value())
                {
                    doextents(id, std::move(op), std::move(ret), entries*2);
                    return;
                }
                exception_ptr e;
                // boost::system::system_error makes no attempt to ask windows for what the error code means :(
                try
                {
                  BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
                }
                catch(...)
                {
                  e=current_exception();
                }
                ret->set_exception(e);
                complete_async_op(id, h, e);
              }
              else
              {
                std::vector<std::pair<off_t, off_t>> out(bytes/sizeof(FILE_ALLOCATED_RANGE_BUFFER));
                for(size_t n=0; n<out.size(); n++)
                {
                  out[n].first=buffers->data()[n].FileOffset.QuadPart;
                  out[n].second=buffers->data()[n].Length.QuadPart;
                }
                ret->set_value(std::move(out));
                complete_async_op(id, h);
              }
            };
            asio::windows::overlapped_ptr ol(threadsource()->io_service(), completion_handler);
            do
            {
                DWORD bytesout=0;
                // Search entire file
                buffers->front().FileOffset.QuadPart=0;
                buffers->front().Length.QuadPart=((off_t)1<<63)-1; // Microsoft claims this is 1<<64-1024 for NTFS, but I get bad parameter error with anything higher than 1<<63-1.
                BOOL ok=DeviceIoControl(p->native_handle(), FSCTL_QUERY_ALLOCATED_RANGES, buffers->data(), sizeof(FILE_ALLOCATED_RANGE_BUFFER), buffers->data(), (DWORD)(buffers->size()*sizeof(FILE_ALLOCATED_RANGE_BUFFER)), &bytesout, ol.get());
                DWORD errcode=GetLastError();
                if(!ok && ERROR_IO_PENDING!=errcode)
                {
                  if(ERROR_INSUFFICIENT_BUFFER==errcode || ERROR_MORE_DATA==errcode)
                  {
                    buffers->resize(buffers->size()*2);
                    continue;
                  }
                  //std::cerr << "ERROR " << errcode << std::endl;
                  asio::error_code ec(errcode, asio::error::get_system_category());
                  ol.complete(ec, ol.get()->InternalHigh);
                }
                else
                  ol.release();
            } while(false);

            // Indicate we're not finished yet
            return std::make_pair(false, h);
        }
        // Called in unknown thread
        completion_returntype doextents(size_t id, async_io_op op, std::shared_ptr<promise<std::vector<std::pair<off_t, off_t>>>> ret)
        {
          return doextents(id, op, std::move(ret), 16);
        }
        // Called in unknown thread
        completion_returntype dostatfs(size_t id, async_io_op op, fs_metadata_flags req, std::shared_ptr<promise<statfs_t>> out)
        {
          try
          {
            windows_nt_kernel::init();
            using namespace windows_nt_kernel;
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
            assert(p);
            statfs_t ret;

            // Probably not worth doing these asynchronously, so execute synchronously
            BOOST_AFIO_TYPEALIGNMENT(8) filesystem::path::value_type buffer[32769];
            IO_STATUS_BLOCK isb={ 0 };
            NTSTATUS ntstat;
            if(!!(req&fs_metadata_flags::flags) || !!(req&fs_metadata_flags::namemax) || !!(req&fs_metadata_flags::fstypename))
            {
              FILE_FS_ATTRIBUTE_INFORMATION *ffai=(FILE_FS_ATTRIBUTE_INFORMATION *) buffer;
              ntstat=NtQueryVolumeInformationFile(h->native_handle(), &isb, ffai, sizeof(buffer), FileFsAttributeInformation);
              if(STATUS_PENDING==ntstat)
                  ntstat=NtWaitForSingleObject(h->native_handle(), FALSE, NULL);
              BOOST_AFIO_ERRHNTFN(ntstat, h->path());
              if(!!(req&fs_metadata_flags::flags))
              {
                ret.f_flags.rdonly=!!(ffai->FileSystemAttributes & FILE_READ_ONLY_VOLUME);
                ret.f_flags.acls=!!(ffai->FileSystemAttributes & FILE_PERSISTENT_ACLS);
                ret.f_flags.xattr=!!(ffai->FileSystemAttributes & FILE_NAMED_STREAMS);
                ret.f_flags.compression=!!(ffai->FileSystemAttributes & FILE_VOLUME_IS_COMPRESSED);
                ret.f_flags.extents=!!(ffai->FileSystemAttributes & FILE_SUPPORTS_SPARSE_FILES);
                ret.f_flags.filecompression=!!(ffai->FileSystemAttributes & FILE_FILE_COMPRESSION);
              }
              if(!!(req&fs_metadata_flags::namemax)) ret.f_namemax=ffai->MaximumComponentNameLength;
              if(!!(req&fs_metadata_flags::fstypename))
              {
                ret.f_fstypename.resize(ffai->FileSystemNameLength/sizeof(filesystem::path::value_type));
                for(size_t n=0; n<ffai->FileSystemNameLength/sizeof(filesystem::path::value_type); n++)
                  ret.f_fstypename[n]=(char) ffai->FileSystemName[n];
              }
            }
            if(!!(req&fs_metadata_flags::bsize) || !!(req&fs_metadata_flags::blocks) || !!(req&fs_metadata_flags::bfree) || !!(req&fs_metadata_flags::bavail))
            {
              FILE_FS_FULL_SIZE_INFORMATION *fffsi=(FILE_FS_FULL_SIZE_INFORMATION *) buffer;
              ntstat=NtQueryVolumeInformationFile(h->native_handle(), &isb, fffsi, sizeof(buffer), FileFsFullSizeInformation);
              if(STATUS_PENDING==ntstat)
                  ntstat=NtWaitForSingleObject(h->native_handle(), FALSE, NULL);
              BOOST_AFIO_ERRHNTFN(ntstat, h->path());
              if(!!(req&fs_metadata_flags::bsize)) ret.f_bsize=fffsi->BytesPerSector*fffsi->SectorsPerAllocationUnit;
              if(!!(req&fs_metadata_flags::blocks)) ret.f_blocks=fffsi->TotalAllocationUnits.QuadPart;
              if(!!(req&fs_metadata_flags::bfree)) ret.f_bfree=fffsi->ActualAvailableAllocationUnits.QuadPart;
              if(!!(req&fs_metadata_flags::bavail)) ret.f_bavail=fffsi->CallerAvailableAllocationUnits.QuadPart;
            }
            if(!!(req&fs_metadata_flags::fsid))
            {
              FILE_FS_OBJECTID_INFORMATION *ffoi=(FILE_FS_OBJECTID_INFORMATION *) buffer;
              ntstat=NtQueryVolumeInformationFile(h->native_handle(), &isb, ffoi, sizeof(buffer), FileFsObjectIdInformation);
              if(STATUS_PENDING==ntstat)
                  ntstat=NtWaitForSingleObject(h->native_handle(), FALSE, NULL);
              if(0/*STATUS_SUCCESS*/==ntstat)
              {
                // FAT32 doesn't support filing system id, so sink error
                memcpy(&ret.f_fsid, ffoi->ObjectId, sizeof(ret.f_fsid));
              }
            }
            if(!!(req&fs_metadata_flags::iosize))
            {
              FILE_FS_SECTOR_SIZE_INFORMATION *ffssi=(FILE_FS_SECTOR_SIZE_INFORMATION *) buffer;
              ntstat=NtQueryVolumeInformationFile(h->native_handle(), &isb, ffssi, sizeof(buffer), FileFsSectorSizeInformation);
              if(STATUS_PENDING==ntstat)
                  ntstat=NtWaitForSingleObject(h->native_handle(), FALSE, NULL);
              if(0/*STATUS_SUCCESS*/!=ntstat)
              {
                  // Windows XP and Vista don't support the FILE_FS_SECTOR_SIZE_INFORMATION
                  // API, so we'll just hardcode 512 bytes
                  ffssi->PhysicalBytesPerSectorForPerformance=512;
              }
              ret.f_iosize=ffssi->PhysicalBytesPerSectorForPerformance;
            }
            if(!!(req&fs_metadata_flags::mntonname))
            {
              UNICODE_STRING *volumepath=(UNICODE_STRING *) buffer;
              ULONG length;
              ntstat=NtQueryObject(h->native_handle(), ObjectNameInformation, volumepath, sizeof(buffer), &length);
              if(STATUS_PENDING==ntstat)
                ntstat=NtWaitForSingleObject(h->native_handle(), FALSE, NULL);
              BOOST_AFIO_ERRHNTFN(ntstat, h->path());
              ret.f_mntonname=filesystem::path::string_type(volumepath->Buffer, volumepath->Length);
            }
            out->set_value(std::move(ret));
            return std::make_pair(true, h);
          }
          catch(...)
          {
            out->set_exception(current_exception());
            throw;
          }
        }
        // Called in unknown thread
        completion_returntype dolock(size_t id, async_io_op op, async_lock_op_req req)
        {
          std::shared_ptr<async_io_handle> h(op.get());
          async_io_handle_windows *p=static_cast<async_io_handle_windows *>(h.get());
          assert(p);
          if(!p->lockfile)
            BOOST_AFIO_THROW(std::invalid_argument("This file handle was not opened with OSLockable."));
          return p->lockfile->lock(id, std::move(op), std::move(req));
        }

    public:
        async_file_io_dispatcher_windows(std::shared_ptr<thread_source> threadpool, file_flags flagsforce, file_flags flagsmask) : async_file_io_dispatcher_base(threadpool, flagsforce, flagsmask), pagesize(page_size())
        {
        }

        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> dir(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::dir, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dodir);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmdir(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmdir, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dormdir);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> file(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::file, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dofile);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmfile(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmfile, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dormfile);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> symlink(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::symlink, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dosymlink);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmsymlink(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmsymlink, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dormsymlink);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> sync(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::sync, ops, async_op_flags::none, &async_file_io_dispatcher_windows::dosync);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> zero(const std::vector<async_io_op> &ops, const std::vector<std::vector<std::pair<off_t, off_t>>> &ranges) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
          for(auto &i: ops)
          {
            if(!i.validate())
              BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
          }
#endif
          return chain_async_ops((int) detail::OpType::zero, ops, ranges, async_op_flags::none, &async_file_io_dispatcher_windows::dozero);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> close(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::close, ops, async_op_flags::none, &async_file_io_dispatcher_windows::doclose);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> read(const std::vector<detail::async_data_op_req_impl<false>> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::read, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::doread);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> write(const std::vector<detail::async_data_op_req_impl<true>> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::write, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dowrite);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> truncate(const std::vector<async_io_op> &ops, const std::vector<off_t> &sizes) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
            if(ops.size()!=sizes.size())
                BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
#endif
            return chain_async_ops((int) detail::OpType::truncate, ops, sizes, async_op_flags::none, &async_file_io_dispatcher_windows::dotruncate);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<std::pair<std::vector<directory_entry>, bool>>>, std::vector<async_io_op>> enumerate(const std::vector<async_enumerate_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::enumerate, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::doenumerate);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<std::vector<std::pair<off_t, off_t>>>>, std::vector<async_io_op>> extents(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::extents, ops, async_op_flags::none, &async_file_io_dispatcher_windows::doextents);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<statfs_t>>, std::vector<async_io_op>> statfs(const std::vector<async_io_op> &ops, const std::vector<fs_metadata_flags> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
            if(ops.size()!=reqs.size())
                BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
#endif
            return chain_async_ops((int) detail::OpType::statfs, ops, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dostatfs);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> lock(const std::vector<async_lock_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::lock, reqs, async_op_flags::none, &async_file_io_dispatcher_windows::dolock);
        }
    };

    struct win_actual_lock_file : public actual_lock_file
    {
      HANDLE h;
      OVERLAPPED ol;
      static BOOST_CONSTEXPR_OR_CONST off_t magicbyte=(1ULL<<62)-1;
      static bool win32_lockmagicbyte(HANDLE h, DWORD flags)
      {
        OVERLAPPED ol={0};
        ol.Offset=(DWORD) (magicbyte & 0xffffffff);
        ol.OffsetHigh=(DWORD) ((magicbyte>>32) & 0xffffffff);
        DWORD len_low=1, len_high=0;
        return LockFileEx(h, LOCKFILE_FAIL_IMMEDIATELY|flags, 0, len_low, len_high, &ol)!=0;
      }
      static bool win32_unlockmagicbyte(HANDLE h)
      {
        OVERLAPPED ol={0};
        ol.Offset=(DWORD) (magicbyte & 0xffffffff);
        ol.OffsetHigh=(DWORD) ((magicbyte>>32) & 0xffffffff);
        DWORD len_low=1, len_high=0;
        return UnlockFileEx(h, 0, len_low, len_high, &ol)!=0;
      }
      win_actual_lock_file(filesystem::path p) : actual_lock_file(std::move(p)), h(nullptr)
      {
        memset(&ol, 0, sizeof(ol));
        bool done=false;
        do
        {
          NTSTATUS status=0;
          for(size_t n=0; n<10; n++)
          {
            // TODO FIXME: Lock file needs to copy exact security descriptor from its original
            std::tie(status, h)=ntcreatefile(async_path_op_req(lockfilepath, file_flags::Create|file_flags::ReadWrite|file_flags::TemporaryFile|file_flags::int_file_share_delete));
            // This may fail with STATUS_DELETE_PENDING, if so sleep and loop
            if(!status)
              break;
            else if(((NTSTATUS) 0xC0000056)/*STATUS_DELETE_PENDING*/==status)
              this_thread::sleep_for(chrono::milliseconds(100));
            else
              BOOST_AFIO_ERRHNT(status);
          }
          BOOST_AFIO_ERRHNT(status);
          // We can't use DeleteOnClose for Samba shares because he'll delete on close even if
          // other processes have a handle open. We hence read lock the same final byte as POSIX considers
          // it (i.e. 1<<62-1). If it fails then the other has a write lock and is about to delete.
          if(!(done=win32_lockmagicbyte(h, 0)))
            CloseHandle(h);
        } while(!done);
      }
      ~win_actual_lock_file()
      {
        // Lock POSIX magic byte for write access. Win32 doesn't permit conversion of shared to exclusive locks
        win32_unlockmagicbyte(h);
        if(win32_lockmagicbyte(h, LOCKFILE_EXCLUSIVE_LOCK))
        {
          // If this lock succeeded then I am potentially the last with this file open
          // so unlink myself, which will work with my open file handle as I was opened with FILE_SHARE_DELETE.
          // All further opens will now fail with STATUS_DELETE_PENDING
          filesystem::path::string_type escapedpath(L"\\\\?\\"+lockfilepath.native());
          BOOST_AFIO_ERRHWIN(DeleteFile(escapedpath.c_str()));
        }
        BOOST_AFIO_ERRHWIN(CloseHandle(h));
      }
      async_file_io_dispatcher_base::completion_returntype lock(size_t id, async_io_op op, async_lock_op_req req, void *_thislockfile) override final
      {
        windows_nt_kernel::init();
        using namespace windows_nt_kernel;
        win_lock_file *thislockfile=(win_lock_file *) _thislockfile;
        auto completion_handler=[this, id, op, req](const asio::error_code &ec)
        {
          std::shared_ptr<async_io_handle> h(op.get());
          if(ec)
          {
            exception_ptr e;
            // boost::system::system_error makes no attempt to ask windows for what the error code means :(
            try
            {
              BOOST_AFIO_ERRGWINFN(ec.value(), h->path());
            }
            catch(...)
            {
              e=current_exception();
            }
            op.parent->complete_async_op(id, h, e);
          }
          else
          {
            op.parent->complete_async_op(id, h);
          }
        };
        // (1<<62)-1 byte used by POSIX for last use detection
        static BOOST_CONSTEXPR_OR_CONST off_t magicbyte=(1ULL<<62)-1;
        if(req.offset==magicbyte)
          BOOST_AFIO_THROW(std::invalid_argument("offset cannot be (1<<62)-1"));
        // If we straddle the magic byte then clamp to just under it
        if(req.offset<magicbyte && req.offset+req.length>magicbyte)
          req.length=std::min(req.offset+req.length, magicbyte)-req.offset;

        NTSTATUS ntstat=0;
        LARGE_INTEGER offset; offset.QuadPart=req.offset;
        LARGE_INTEGER length; length.QuadPart=req.length;
        if(req.type==async_lock_op_req::Type::unlock)
        {
          ntstat=NtUnlockFile(h, (PIO_STATUS_BLOCK) &ol, &offset, &length, 0);
          //printf("UL %u ntstat=%u\n", id, ntstat);
        }
        else
        {
          ntstat=NtLockFile(h, thislockfile->ev.native_handle(), nullptr, nullptr, (PIO_STATUS_BLOCK) &ol, &offset, &length, 0,
            false, req.type==async_lock_op_req::Type::write_lock);
          //printf("L %u ntstat=%u\n", id, ntstat);
        }
        if(STATUS_PENDING!=ntstat)
        {
          SetWin32LastErrorFromNtStatus(ntstat);
          asio::error_code ec(GetLastError(), asio::error::get_system_category());
          completion_handler(ec);
        }
        else
          thislockfile->ev.async_wait(completion_handler);
        // Indicate we're not finished yet
        return std::make_pair(false, std::shared_ptr<async_io_handle>());
      }
    };
} // namespace
BOOST_AFIO_V1_NAMESPACE_END

#endif
