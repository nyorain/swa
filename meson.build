project(
	'swa',
	'c',
	version: '0.1.0',
	license: 'MIT',
	meson_version: '>=0.48.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'werror=true',
	],
)

add_project_arguments(
	[
		'-Wno-unused-parameter',
		'-Wundef',
		'-Wvla',
	],
	language: 'c',
)

cc = meson.get_compiler('c')
swa_src = files(
	'src/swa/swa.c',
	'src/swa/xkb.c',
	'src/swa/wayland.c',
)

source_root = '/'.join(meson.source_root().split('\\'))
flag_dlg = '-DDLG_BASE_PATH="' + source_root + '/"'
add_project_arguments(flag_dlg, language: 'c')

dep_pml = dependency('pml',
	fallback: ['pml', 'pml_dep'],
	static: true
)
dep_dlg = dependency('dlg',
	fallback: ['dlg', 'dlg_dep'],
	static: true
)

dep_vulkan = dependency('vulkan', required: true)
dep_xkbcommon = dependency('xkbcommon', required: true)

dep_wl_client = dependency('wayland-client', required: true)
dep_wl_cursor = dependency('wayland-cursor', required: true)
dep_wl_egl = dependency('wayland-egl', required: true)
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
]

conf_data = configuration_data()
conf_data.set('SWA_WITH_VK', true, description: 'Compiled with Vulkan support')
conf_data.set('SWA_WITH_GL', true, description: 'Compiled with OpenGL support')
subdir('include/swa')

swa_inc = [
	include_directories('src'),
	include_directories('include'),
]

lib_swa = library('swa',
	swa_src,
	include_directories: swa_inc,
	dependencies: swa_deps,
	install: true)

swa_dep = declare_dependency(
	link_with: lib_swa,
	include_directories: include_directories('include'))

executable('example',
	'example.c',
	dependencies: [swa_dep, dep_dlg])
