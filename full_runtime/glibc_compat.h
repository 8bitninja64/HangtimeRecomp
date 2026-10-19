/* Linux portability: bind the few glibc symbols that new glibc headers version very recently to
 * their original x86-64 versions (GLIBC_2.2.5), so binaries built on a current distribution also
 * run on older ones such as SteamOS. Force-included into every Linux translation unit of the
 * runtime and RT64 (CMakeLists.txt, build.py, build_linux.sh); check with
 *   objdump -T hangtime | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
 * The C23 entry points (__isoc23_*) differ from the originals only in accepting "0b" binary
 * prefixes, which nothing here uses. A directive for a symbol a file does not reference is inert.
 */
#if defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__)
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver __isoc23_sscanf,sscanf@GLIBC_2.2.5");
__asm__(".symver __isoc23_vsscanf,vsscanf@GLIBC_2.2.5");
__asm__(".symver __isoc23_fscanf,fscanf@GLIBC_2.2.5");
__asm__(".symver __isoc23_strtol,strtol@GLIBC_2.2.5");
__asm__(".symver __isoc23_strtoll,strtoll@GLIBC_2.2.5");
__asm__(".symver __isoc23_strtoul,strtoul@GLIBC_2.2.5");
__asm__(".symver __isoc23_strtoull,strtoull@GLIBC_2.2.5");
#endif
