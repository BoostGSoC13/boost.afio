/* storage_profile.hpp
A profile of an OS and filing system
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: Dec 2015


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "../../../storage_profile.hpp"
#include "../../../handle.hpp"
#include "import.hpp"

#include <winioctl.h>

BOOST_AFIO_V2_NAMESPACE_BEGIN

namespace storage_profile
{
  namespace system
  {
    // OS name, version
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 6387) // MSVC sanitiser warns that GetModuleHandleA() might fail (hah!)
#endif
    outcome<void> os(storage_profile &sp, file_handle &h) noexcept
    {
      static std::string os_name, os_ver;
      if (!os_name.empty())
      {
        sp.os_name.value = os_name;
        sp.os_ver.value = os_ver;
      }
      else
      {
        try
        {
          RTL_OSVERSIONINFOW ovi = { sizeof(RTL_OSVERSIONINFOW) };
          // GetVersionEx() is no longer useful since Win8.1
          using RtlGetVersion_t = LONG(*)(PRTL_OSVERSIONINFOW);
          static RtlGetVersion_t RtlGetVersion;
          if (!RtlGetVersion)
            RtlGetVersion = (RtlGetVersion_t)GetProcAddress(GetModuleHandle(L"NTDLL.DLL"), "RtlGetVersion");
          if (!RtlGetVersion)
            return make_errored_outcome<void>(GetLastError());
          RtlGetVersion(&ovi);
          sp.os_name.value = "Microsoft Windows ";
          sp.os_name.value.append(ovi.dwPlatformId == VER_PLATFORM_WIN32_NT ? "NT" : "Unknown");
          sp.os_ver.value.append(to_string(ovi.dwMajorVersion) + "." + to_string(ovi.dwMinorVersion) + "." + to_string(ovi.dwBuildNumber));
          os_name = sp.os_name.value;
          os_ver = sp.os_ver.value;
        }
        catch (...)
        {
          return std::current_exception();
        }
      }
      return make_ready_outcome<void>();
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    // CPU name, architecture, physical cores
    outcome<void> cpu(storage_profile &sp, file_handle &h) noexcept
    {
      static std::string cpu_name, cpu_architecture;
      static unsigned cpu_physical_cores;
      if (!cpu_name.empty())
      {
        sp.cpu_name.value = cpu_name;
        sp.cpu_architecture.value = cpu_architecture;
        sp.cpu_physical_cores.value = cpu_physical_cores;
      }
      else
      {
        try
        {
          SYSTEM_INFO si = { {sizeof(SYSTEM_INFO)} };
          GetNativeSystemInfo(&si);
          switch (si.wProcessorArchitecture)
          {
          case PROCESSOR_ARCHITECTURE_AMD64:
            sp.cpu_name.value = sp.cpu_architecture.value = "x64";
            break;
          case PROCESSOR_ARCHITECTURE_ARM:
            sp.cpu_name.value = sp.cpu_architecture.value = "ARM";
            break;
          case PROCESSOR_ARCHITECTURE_IA64:
            sp.cpu_name.value = sp.cpu_architecture.value = "IA64";
            break;
          case PROCESSOR_ARCHITECTURE_INTEL:
            sp.cpu_name.value = sp.cpu_architecture.value = "x86";
            break;
          default:
            sp.cpu_name.value = sp.cpu_architecture.value = "unknown";
            break;
          }
          {
            DWORD size = 0;

            GetLogicalProcessorInformation(NULL, &size);
            if (ERROR_INSUFFICIENT_BUFFER != GetLastError())
              return make_errored_outcome<void>(GetLastError());

            std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(size);
            if (GetLogicalProcessorInformation(&buffer.front(), &size) == FALSE)
              return make_errored_outcome<void>(GetLastError());

            const size_t Elements = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

            sp.cpu_physical_cores.value = 0;
            for (size_t i = 0; i < Elements; ++i) {
              if (buffer[i].Relationship == RelationProcessorCore)
                ++sp.cpu_physical_cores.value;
            }
          }
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
          // We can do a much better CPU name on x86/x64
          sp.cpu_name.value.clear();
          {
            char buffer[62];
            memset(buffer, 32, 62);
            int nBuff[4];
            __cpuid(nBuff, 0);
            *(int*)&buffer[0] = nBuff[1];
            *(int*)&buffer[4] = nBuff[3];
            *(int*)&buffer[8] = nBuff[2];

            // Do we have a brand string?
            __cpuid(nBuff, 0x80000000);
            if ((unsigned)nBuff[0] >= 0x80000004)
            {
              __cpuid((int*)&buffer[14], 0x80000002);
              __cpuid((int*)&buffer[30], 0x80000003);
              __cpuid((int*)&buffer[46], 0x80000004);
            }
            else
              strcpy(&buffer[14], "unbranded");

            // Trim string
            for (size_t n = 0; n < 62; n++)
            {
              if (!n || buffer[n] != 32 || buffer[n - 1] != 32)
                if (buffer[n])
                  sp.cpu_name.value.push_back(buffer[n]);
            }
          }
#endif
          cpu_name = sp.cpu_name.value;
          cpu_architecture = sp.cpu_architecture.value;
          cpu_physical_cores = sp.cpu_physical_cores.value;
        }
        catch (...)
        {
          return std::current_exception();
        }
      }
      return make_ready_outcome<void>();
    }
    namespace windows
    {
      outcome<void> _mem(storage_profile &sp, file_handle &h) noexcept
      {
        MEMORYSTATUSEX ms = { sizeof(MEMORYSTATUSEX) };
        GlobalMemoryStatusEx(&ms);
        sp.mem_quantity.value = (unsigned long long)ms.ullTotalPhys;
        sp.mem_in_use.value = (float)(ms.ullTotalPhys - ms.ullAvailPhys) / ms.ullTotalPhys;
        return make_ready_outcome<void>();
      }
    }
  }
  namespace storage
  {
    namespace windows
    {
      // Controller type, max transfer, max buffers. Device name, size
      outcome<void> _device(storage_profile &sp, file_handle &h, std::string mntfromname) noexcept
      {
        try
        {
          alignas(8) fixme_path::value_type buffer[32769];
          // Firstly open a handle to the volume
          BOOST_OUTCOME_FILTER_ERROR(volumeh, file_handle::file(*h.service(), mntfromname, handle::mode::none, handle::creation::open_existing, handle::caching::only_metadata));
          STORAGE_PROPERTY_QUERY spq = { StorageAdapterProperty, PropertyStandardQuery };
          STORAGE_ADAPTER_DESCRIPTOR *sad = (STORAGE_ADAPTER_DESCRIPTOR *)buffer;
          OVERLAPPED ol = { (ULONG_PTR)-1 };
          if (!DeviceIoControl(volumeh.native_handle().h, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), sad, sizeof(buffer), nullptr, &ol))
          {
            if (ERROR_IO_PENDING == GetLastError())
            {
              NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol);
              if (ntstat)
                return make_errored_outcome_nt<void>(ntstat);
            }
            if (ERROR_SUCCESS != GetLastError())
              return make_errored_outcome<void>(GetLastError());
          }
          switch (sad->BusType)
          {
          case BusTypeScsi:
            sp.controller_type.value = "SCSI";
            break;
          case BusTypeAtapi:
            sp.controller_type.value = "ATAPI";
            break;
          case BusTypeAta:
            sp.controller_type.value = "ATA";
            break;
          case BusType1394:
            sp.controller_type.value = "1394";
            break;
          case BusTypeSsa:
            sp.controller_type.value = "SSA";
            break;
          case BusTypeFibre:
            sp.controller_type.value = "Fibre";
            break;
          case BusTypeUsb:
            sp.controller_type.value = "USB";
            break;
          case BusTypeRAID:
            sp.controller_type.value = "RAID";
            break;
          case BusTypeiScsi:
            sp.controller_type.value = "iSCSI";
            break;
          case BusTypeSas:
            sp.controller_type.value = "SAS";
            break;
          case BusTypeSata:
            sp.controller_type.value = "SATA";
            break;
          case BusTypeSd:
            sp.controller_type.value = "SD";
            break;
          case BusTypeMmc:
            sp.controller_type.value = "MMC";
            break;
          case BusTypeVirtual:
            sp.controller_type.value = "Virtual";
            break;
          case BusTypeFileBackedVirtual:
            sp.controller_type.value = "File Backed Virtual";
            break;
          default:
            sp.controller_type.value = "unknown";
            break;
          }
          sp.controller_max_transfer.value = sad->MaximumTransferLength;
          sp.controller_max_buffers.value = sad->MaximumPhysicalPages;

          // Now ask the volume what physical disks it spans
          VOLUME_DISK_EXTENTS *vde = (VOLUME_DISK_EXTENTS *)buffer;
          ol.Internal = (ULONG_PTR)-1;
          if (!DeviceIoControl(volumeh.native_handle().h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, vde, sizeof(buffer), nullptr, &ol))
          {
            if (ERROR_IO_PENDING == GetLastError())
            {
              NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol);
              if (ntstat)
                return make_errored_outcome_nt<void>(ntstat);
            }
            if (ERROR_SUCCESS != GetLastError())
              return make_errored_outcome<void>(GetLastError());
          }
          DWORD disk_extents = vde->NumberOfDiskExtents;
          sp.device_name.value.clear();
          if (disk_extents > 0)
          {
            // For now we only care about the first physical device
            alignas(8) fixme_path::value_type physicaldrivename[32769];
            wsprintf(physicaldrivename, L"\\\\.\\PhysicalDrive%u", vde->Extents[0].DiskNumber);
            BOOST_OUTCOME_FILTER_ERROR(diskh, file_handle::file(*h.service(), physicaldrivename, handle::mode::none, handle::creation::open_existing, handle::caching::only_metadata));
            spq = { StorageDeviceProperty, PropertyStandardQuery };
            STORAGE_DEVICE_DESCRIPTOR *sdd = (STORAGE_DEVICE_DESCRIPTOR *)buffer;
            ol.Internal = (ULONG_PTR)-1;
            if (!DeviceIoControl(diskh.native_handle().h, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), sdd, sizeof(buffer), nullptr, &ol))
            {
              if (ERROR_IO_PENDING == GetLastError())
              {
                NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol);
                if (ntstat)
                  return make_errored_outcome_nt<void>(ntstat);
              }
              if (ERROR_SUCCESS != GetLastError())
                return make_errored_outcome<void>(GetLastError());
            }
            if (sdd->VendorIdOffset > 0 && sdd->VendorIdOffset < sizeof(buffer))
            {
              for (auto n = sdd->VendorIdOffset; ((const char *)buffer)[n]; n++)
                sp.device_name.value.push_back(((const char *)buffer)[n]);
              sp.device_name.value.push_back(',');
            }
            if (sdd->ProductIdOffset > 0 && sdd->ProductIdOffset < sizeof(buffer))
            {
              for (auto n = sdd->ProductIdOffset; ((const char *)buffer)[n]; n++)
                sp.device_name.value.push_back(((const char *)buffer)[n]);
              sp.device_name.value.push_back(',');
            }
            if (sdd->ProductRevisionOffset > 0 && sdd->ProductRevisionOffset < sizeof(buffer))
            {
              for (auto n = sdd->ProductRevisionOffset; ((const char *)buffer)[n]; n++)
                sp.device_name.value.push_back(((const char *)buffer)[n]);
              sp.device_name.value.push_back(',');
            }
            if (!sp.device_name.value.empty())
              sp.device_name.value.resize(sp.device_name.value.size() - 1);
            if (disk_extents > 1)
              sp.device_name.value.append(" (NOTE: plus additional devices)");

            // Get device size
            // IOCTL_STORAGE_READ_CAPACITY needs GENERIC_READ privs which requires admin privs
            // so simply fetch the geometry
            DISK_GEOMETRY_EX *dg = (DISK_GEOMETRY_EX *)buffer;
            ol.Internal = (ULONG_PTR)-1;
            if (!DeviceIoControl(diskh.native_handle().h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, dg, sizeof(buffer), nullptr, &ol))
            {
              if (ERROR_IO_PENDING == GetLastError())
              {
                NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol);
                if (ntstat)
                  return make_errored_outcome_nt<void>(ntstat);
              }
              if (ERROR_SUCCESS != GetLastError())
                return make_errored_outcome<void>(GetLastError());
            }
            sp.device_size.value = dg->DiskSize.QuadPart;
          }
        }
        catch (...)
        {
          return std::current_exception();
        }
        return make_ready_outcome<void>();
      }
    }
  }
}

BOOST_AFIO_V2_NAMESPACE_END
