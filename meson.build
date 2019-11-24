project(
	'swa',
	'c',
	version: '0.1.0',
	license: 'MIT',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c99',
		'warning_level=3',
		'werror=true',
	],
)

shared = get_option('shared')

cc = meson.get_compiler('c')
args = cc.get_supported_arguments([
	# gcc/clang
	'-Wno-unused-parameter',
	'-Wundef',
	'-Wvla',

	# msvc
	'/wd4127', # conditional expression is constant
	'/wd4706', # assignment within conditional expression
	'/wd4100', # unreferenced formal parameter

	# c99 compat
	'/wd4132', # const object should be initialized (for fwd decl)
	'/wd4204', # non-constant aggregate initializer
])
add_project_arguments(args, language: 'c')

swa_src = files('src/swa/swa.c')

source_root = '/'.join(meson.source_root().split('\\'))
flag_dlg = '-DDLG_BASE_PATH="' + source_root + '/"'
add_project_arguments(flag_dlg, language: 'c')

dep_dlg = dependency('dlg',
	fallback: ['dlg', 'dlg_dep'],
	static: true
)

swa_args = []
conf_data = configuration_data()
conf_data.set('SWA_SHARED', shared, description: 'Compiled as shared library')

if build_machine.system() == 'windows'
	swa_deps = [dep_dlg]

	conf_data.set('SWA_WITH_VK', false, description: 'Compiled with Vulkan support')
	conf_data.set('SWA_WITH_GL', false, description: 'Compiled with OpenGL support')
	conf_data.set('SWA_WITH_WL', false, description: 'Compiled with Wayland support')
	conf_data.set('SWA_WITH_X11', false, description: 'Compiled with X11 support')
	conf_data.set('SWA_WITH_WIN', true, description: 'Compiled with Winapi support')

	if shared
		swa_args += '-DSWA_API=__declspec(dllexport)'
	endif

	swa_src += files('src/swa/winapi.c')
else
	dep_pml = dependency('pml',
		fallback: ['pml', 'pml_dep'],
		static: true
	)

	swa_src += files(
		'src/swa/xkb.c',
		'src/swa/xcursor.c',
		'src/swa/wayland.c',
		'src/swa/x11.c',
		'src/swa/egl.c',
	)

	dep_vulkan = dependency('vulkan', required: true)
	dep_xkbcommon = dependency('xkbcommon', required: true)

	dep_wl_client = dependency('wayland-client', required: true)
	dep_wl_cursor = dependency('wayland-cursor', required: true)
	dep_wl_egl = dependency('wayland-egl', required: true)
	dep_egl = dependency('egl', required: true, version: '>=1.4')

	op_enable_x11 = true
	x11_deps = [
		dep_xkbcommon,
		dependency('x11', required: op_enable_x11),
		dependency('x11-xcb', required: op_enable_x11),
		dependency('xcursor', required: op_enable_x11),
		dependency('xcb', required: op_enable_x11),
		dependency('xcb-ewmh', required: op_enable_x11),
		dependency('xcb-icccm', required: op_enable_x11),
		dependency('xcb-shm', required: op_enable_x11),
		dependency('xcb-present', required: op_enable_x11),
		dependency('xcb-xinput', required: op_enable_x11),
		dependency('xcb-xkb', required: op_enable_x11),
		dependency('xkbcommon-x11', required: op_enable_x11),
	]

	wl_protos = dependency('wayland-protocols', version: '>=1.14', required: true)
	wl_scanner = find_program('wayland-scanner', required: true)
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

	swa_deps = [
		dep_pml,
		dep_dlg,
		dep_wl_client,
		dep_wl_cursor,
		dep_wl_egl,
		dep_wl_protos,
		dep_xkbcommon,
		dep_vulkan,
		dep_egl,
	]

	swa_deps += x11_deps

	conf_data.set('SWA_WITH_VK', true, description: 'Compiled with Vulkan support')
	conf_data.set('SWA_WITH_GL', true, description: 'Compiled with OpenGL support')
	conf_data.set('SWA_WITH_WL', true, description: 'Compiled with Wayland support')
	conf_data.set('SWA_WITH_X11', true, description: 'Compiled with X11 support')
	conf_data.set('SWA_WITH_WIN', false, description: 'Compiled with Winapi support')
endif

subdir('include/swa')

swa_inc = [
	include_directories('src'),
	include_directories('include'),
]

if shared
	lib_type = 'shared_library'
else
	lib_type = 'static_library'
endif

lib_swa = build_target('swa',
	swa_src,
	include_directories: swa_inc,
	dependencies: swa_deps,
	c_args: swa_args,
	target_type: lib_type,
	install: true)

swa_dep = declare_dependency(
	link_with: lib_swa,
	include_directories: include_directories('include'))

executable('example',
	'example.c',
	dependencies: [swa_dep, dep_dlg])

executable('example-buffer',
	'example-buffer.c',
	dependencies: [swa_dep, dep_dlg])

executable('example-exchange',
	'example-exchange.c',
	dependencies: [swa_dep, dep_dlg])

executable('example-vulkan',
	'example-vulkan.c',
	dependencies: [swa_dep, dep_dlg, dep_vulkan])

executable('example-gl',
	'example-gl.c',
	dependencies: [swa_dep, dep_dlg, dependency('gl', required: true)])

dep_threads = dependency('threads')
executable('example-wakeup',
	'example-wakeup.c',
	dependencies: [swa_dep, dep_threads, dep_dlg])
