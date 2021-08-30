project('lite-xl',
    'c',
    version : '2.0.1',
    license : 'MIT',
    meson_version : '>= 0.55',
    default_options : ['c_std=gnu11']
)

#===============================================================================
# Configuration
#===============================================================================
conf_data = configuration_data()
conf_data.set('PROJECT_BUILD_DIR', meson.current_build_dir())
conf_data.set('PROJECT_SOURCE_DIR', meson.current_source_dir())
conf_data.set('PROJECT_VERSION', meson.project_version())

if host_machine.system() == 'windows'
    configure_file(
        input : 'scripts/innosetup/innosetup.iss.in',
        output : 'innosetup.iss',
        configuration : conf_data
    )
endif
#===============================================================================
# Compiler Settings
#===============================================================================
if host_machine.system() == 'darwin'
    add_languages('objc')
endif

cc = meson.get_compiler('c')

lite_cargs = []
#===============================================================================
# Linker Settings
#===============================================================================
lite_link_args = []
if cc.get_id() == 'gcc' and get_option('buildtype') == 'release'
    lite_link_args += ['-static-libgcc']
endif

if host_machine.system() == 'darwin'
    lite_link_args += ['-framework', 'CoreServices', '-framework', 'Foundation']
endif
#===============================================================================
# Dependencies
#===============================================================================
libm = cc.find_library('m', required : false)
libdl = cc.find_library('dl', required : false)
lua_dep = dependency('lua5.2', fallback: ['lua', 'lua_dep'],
    default_options: ['shared=false', 'use_readline=false', 'app=false']
)
pcre2_dep = dependency('libpcre2-8')
sdl_dep = dependency('sdl2', method: 'config-tool')
reproc_dep = dependency('reproc', fallback: ['reproc', 'reproc_dep'],
    default_options: [
        'default_library=static', 'multithreaded=false',
        'reproc-cpp=false', 'examples=false'
    ]
)
freetype_dep = dependency('freetype2')

cmake = import('cmake')
nanovg_opt = cmake.subproject_options()
nanovg_opt.add_cmake_defines({
  'NANOVG_SHARED_LIBS': false,
  'NANOVG_BUILD_EXAMPLES': false,
  'NANOVG_BUILD_SVG': false,
  'NANOVG_BUILD_GUI': false,
  'NANOVG_USE_FREETYPE': true,
  'NANOVG_USE_STB': false,
})
nanovg = cmake.subproject('nanovg', options: nanovg_opt)
nanovg_dep = [nanovg.dependency('nanovg'), nanovg.dependency('nanovg_gl2')]
lite_deps = [lua_dep, sdl_dep, reproc_dep, pcre2_dep, freetype_dep, libm, libdl, nanovg_dep] 

if host_machine.system() == 'windows'
    # Note that we need to explicitly add the windows socket DLL because
    # the pkg-config file from reproc does not include it.
    lite_deps += cc.find_library('ws2_32', required: true)
    lite_deps += cc.find_library('opengl32', required: true)
    lite_deps += cc.find_library('glew32', required: true)
else if host_machine.system() == 'darwin'
    lite_link_args += ['-framework', 'OpenGL']
else
    lite_deps += cc.find_library('GL', required: true)
    lite_deps += cc.find_library('GLEW', required: true)
endif
#===============================================================================
# Install Configuration
#===============================================================================
if get_option('portable') or host_machine.system() == 'windows'
    lite_bindir = '/'
    lite_docdir = '/doc'
    lite_datadir = '/data'
elif get_option('bundle') and host_machine.system() == 'darwin'
    lite_cargs += '-DMACOS_USE_BUNDLE'
    lite_bindir = 'Contents/MacOS'
    lite_docdir = 'Contents/Resources'
    lite_datadir = 'Contents/Resources'
    install_data('resources/icons/icon.icns', install_dir : 'Contents/Resources')
    install_data('resources/macos/Info.plist', install_dir : 'Contents')
else
    lite_bindir = 'bin'
    lite_docdir = 'share/doc/lite-xl'
    lite_datadir = 'share/lite-xl'
    if host_machine.system() == 'linux'
        install_data('resources/icons/lite-xl.svg',
            install_dir : 'share/icons/hicolor/scalable/apps'
        )
        install_data('resources/linux/org.lite-xl.lite-xl.desktop',
            install_dir : 'share/applications'
        )
        install_data('resources/linux/org.lite-xl.lite-xl.appdata.xml',
            install_dir : 'share/metainfo'
        )
    endif
endif

install_data('licenses/licenses.md', install_dir : lite_docdir)

install_subdir('data' / 'core' , install_dir : lite_datadir, exclude_files : 'start.lua')
foreach data_module : ['fonts', 'plugins', 'colors']
    install_subdir('data' / data_module , install_dir : lite_datadir)
endforeach

configure_file(
    input : 'data/core/start.lua',
    output : 'start.lua',
    configuration : conf_data,
    install : true,
    install_dir : lite_datadir / 'core',
)

#===============================================================================
# Targets
#===============================================================================
subdir('src')
