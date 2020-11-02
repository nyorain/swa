project(
	'swa',
	'c',
	version: '0.1.0',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'werror=true',
	],
)

examples = get_option('examples')

opt_with_gl = get_option('with-gl')
opt_with_vulkan = get_option('with-vulkan')
opt_link_vulkan = get_option('link-vulkan')
opt_with_wayland = get_option('with-wayland')
opt_with_x11 = get_option('with-x11')
opt_with_kms = get_option('with-kms')

cc = meson.get_compiler('c')
args = cc.get_supported_arguments([
	# gcc/clang
	'-Wno-unused-parameter',
	'-Wno-unused-function',
	'-Wno-missing-braces',
	'-Wundef',
	'-Wvla',

	# msvc
	'/wd4127', # conditional expression is constant
	'/wd4706', # assignment within conditional expression
	'/wd4100', # unreferenced formal parameter
	'/wd4189', # local variable unused
	'/wd4090', # wrong const double pointer warnings

	# c99 & c11 compat
	'/wd4132', # const object should be initialized (for fwd decl)
	'/wd4204', # non-constant aggregate initializer
	'/wd4201', # anonymous union extension
])

if meson.get_compiler('c').get_id() == 'clang'
	args += '-Wno-missing-braces'
endif

add_project_arguments(args, language: 'c')

swa_src = files('src/swa/swa.c')

source_root = '/'.join(meson.source_root().split('\\'))
flag_dlg = '-DDLG_BASE_PATH="' + source_root + '/"'
add_project_arguments(flag_dlg, language: 'c')

dep_threads = dependency('threads', required: false)
dep_dlg = dependency('dlg',
	fallback: ['dlg', 'dlg_dep'],
)

shared = (get_option('default_library') == 'shared')

swa_deps = [dep_dlg, dep_threads]
swa_args = []
conf_data = configuration_data()
conf_data.set('SWA_SHARED', shared, description: 'Compiled as shared library')

dep_vulkan_full = dependency('vulkan', required: opt_with_vulkan)
dep_vulkan = dep_vulkan_full
if not opt_link_vulkan
	dep_vulkan = dep_vulkan.partial_dependency(
		compile_args: true,
		includes: true)
endif

swa_inc = [
	include_directories('src'),
	include_directories('include'),
]

with_x11 = false
with_wl = false
with_win = false
with_android = false
with_kms = false
with_gl = false
with_vk = dep_vulkan.found()

if host_machine.system() == 'android'
	dep_egl = cc.find_library('EGL', required: opt_with_gl)
	dep_log = cc.find_library('log')
	dep_android = cc.find_library('android')

	with_android = true
	with_gl = dep_egl.found()

	swa_src += files('src/swa/android.c')

	if with_gl
		swa_src += files('src/swa/egl.c')
	endif

	swa_deps += [
		dep_log,
		dep_android,
		dep_egl,
		dep_vulkan,
	]
elif host_machine.system() == 'windows'
	dep_gl = dependency('gl', required: opt_with_gl)

	with_win = true
	win_gl = dep_gl.found()

	if shared
		swa_args += '-DSWA_API=__declspec(dllexport)'
	endif

	swa_args += '-D_WIN32_WINNT=0x0600'

	swa_deps += cc.find_library('Dwmapi')
	swa_deps += cc.find_library('Shlwapi')
	swa_deps += cc.find_library('Gdi32')
	swa_deps += dep_vulkan
	swa_deps += dep_gl
	swa_src += files(
		'src/swa/winapi.c',
		'src/swa/wgl.c'
	)
else # just assume some posix/unix variant
	dep_pml = dependency('pml',
		fallback: ['pml', 'pml_dep'],
		static: true,
		default_options: ['examples=false', 'tests=false'],
	)

	# we require xkbcommon since it's needed by all backends
	dep_xkbcommon = dependency('xkbcommon', required: true)

	swa_src += files(
		'src/swa/xkb.c',
		'src/swa/xcursor.c',
	)

	dep_egl = dependency('egl', required: opt_with_gl, version: '>=1.4')
	with_gl = dep_egl.found()

	if with_gl
		swa_src += files('src/swa/egl.c')
	endif

	swa_deps += [
		dep_xkbcommon,
		dep_pml,
		dep_vulkan,
		dep_egl,
	]

	# == wayland ==
	dep_wl_client = dependency('wayland-client', required: opt_with_wayland)
	dep_wl_cursor = dependency('wayland-cursor', required: opt_with_wayland)
	wl_protos = dependency('wayland-protocols', version: '>=1.14', required: opt_with_wayland)
	wl_scanner = find_program('wayland-scanner', required: opt_with_wayland)

	with_wl = dep_wl_client.found() and dep_wl_cursor.found() and wl_protos.found() and wl_scanner.found()

	# When wayland and egl is found, wayland-egl must be found as well.
	if with_wl
		dep_wl_egl = dependency('wayland-egl', required: with_gl and with_wl)
		swa_src += files(
			'src/swa/wayland.c',
		)

		wl_protocol_dir = wl_protos.get_pkgconfig_variable('pkgdatadir')

		if dep_wl_client.version().version_compare('>=1.14.91')
			code_type = 'private-code'
		else
			code_type = 'code'
		endif

		wl_scanner_code = generator(
			wl_scanner,
			output: '@BASENAME@-protocol.c',
			arguments: [code_type, '@INPUT@', '@OUTPUT@'],
		)

		wl_scanner_client = generator(
			wl_scanner,
			output: '@BASENAME@-client-protocol.h',
			arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
		)

		wl_protos_src = []
		wl_protos_headers = []

		wl_protocols = [
			[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
			[wl_protocol_dir, 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
		]

		foreach p : wl_protocols
			xml = join_paths(p)
			wl_protos_src += wl_scanner_code.process(xml)
			wl_protos_headers += wl_scanner_client.process(xml)
		endforeach

		lib_wl_protos = static_library(
			'client_protos',
			wl_protos_src + wl_protos_headers,
			dependencies: [dep_wl_client]
		) # for the include directory

		dep_wl_protos = declare_dependency(
			link_with: lib_wl_protos,
			sources: wl_protos_headers,
		)

		swa_deps += [
			dep_wl_client,
			dep_wl_cursor,
			dep_wl_egl,
			dep_wl_protos,
		]
	endif

	# == x11 ==
	x11_deps = [
		dep_xkbcommon,
		dependency('x11', required: opt_with_x11),
		dependency('x11-xcb', required: opt_with_x11),
		dependency('xcursor', required: opt_with_x11),
		dependency('xcb', required: opt_with_x11),
		dependency('xcb-ewmh', required: opt_with_x11),
		dependency('xcb-icccm', required: opt_with_x11),
		dependency('xcb-shm', required: opt_with_x11),
		dependency('xcb-present', required: opt_with_x11),
		dependency('xcb-xinput', required: opt_with_x11),
		dependency('xcb-xkb', required: opt_with_x11),
		dependency('xkbcommon-x11', required: opt_with_x11),
	]

	foreach dep : x11_deps
		if not dep.found()
			with_x11 = false
		endif
	endforeach

	if with_x11
		swa_src += files(
			'src/swa/x11.c',
		)

		swa_deps += x11_deps
	endif


	# kms/drm backend
	dep_drm = dependency('libdrm', version: '>=2.4.95', required: opt_with_kms)
	dep_gbm = dependency('gbm', version: '>=17.1.0', required: opt_with_kms)
	dep_libinput = dependency('libinput', version: '>=1.9.0', required: opt_with_kms)
	dep_udev = dependency('libudev', required: opt_with_kms)

	with_kms = dep_drm.found() and dep_gbm.found() and dep_libinput.found() and dep_udev.found()
	if with_kms
		if with_vk
			if not opt_link_vulkan
				error('Combining kms backend and link-vulkan=false does not work')
			endif

			swa_src += files(
				'src/swa/kms/vulkan.c'
			)
		endif

		swa_src += files(
			'src/swa/kms/kms.c',
			'src/swa/kms/props.c',
			'src/swa/kms/xcursor.c',
		)

		swa_deps += [
			dep_drm,
			dep_gbm,
			dep_libinput,
			dep_udev,
		]
	endif

	# check that at least one backend is available
	if not with_wl and not with_x11 and not with_kms
		error('No backend enabled')
	endif
endif

conf_data.set('SWA_WITH_VK', with_vk, description: 'Compiled with Vulkan support')
conf_data.set('SWA_WITH_LINKED_VK', opt_link_vulkan, description: 'Linked to vulkan library')
conf_data.set('SWA_WITH_ANDROID', with_android, description: 'Compiled with Android support')
conf_data.set('SWA_WITH_GL', with_gl, description: 'Compiled with OpenGL support')
conf_data.set('SWA_WITH_WL', with_wl, description: 'Compiled with Wayland support')
conf_data.set('SWA_WITH_X11', with_x11, description: 'Compiled with X11 support')
conf_data.set('SWA_WITH_WIN', with_win, description: 'Compiled with Winapi support')
conf_data.set('SWA_WITH_KMS', with_kms, description: 'Compiled with KMS/DRM support')

subdir('include/swa')

if with_android
	swa_lib = shared_module('swa',
		swa_src,
		include_directories: swa_inc,
		dependencies: swa_deps,
		c_args: swa_args,
		install: true)

	exe_type = 'shared_library'
else
	swa_lib = library('swa',
		swa_src,
		include_directories: swa_inc,
		dependencies: swa_deps,
		c_args: swa_args,
		install: true)

	exe_type = 'executable'
endif

swa_dep = declare_dependency(
	link_with: swa_lib,
	include_directories: include_directories('include'))

if examples
	subdir('docs/examples')
endif

if with_android and examples
	subdir('apk')
endif
