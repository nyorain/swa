apks = {
	'ex_buffer': ex_buffer,
	'ex_vulkan': ex_vulkan,
	'ex_vulkan_loop': ex_vulkan_loop,
	'ex_gl': ex_gl,
	'ex_wakeup': ex_wakeup,
	'ex_simple': ex_simple,
	'ex_input': ex_input,
}

setupsh = files('setup.sh')
buildsh = files('build.sh')

arch = host_machine.cpu()
bt = meson.get_cross_property('swa_android_buildtools')
platform = meson.get_cross_property('swa_android_platform')

# NOTE: workaround for https://github.com/mesonbuild/meson/issues/3589
# seems like inputs from files() are always passed as relative paths.
# But we change the folder in the build script, breaking those paths
# manifest = meson.current_source_dir() / 'AndroidManifest32.xml.in'
manifest = meson.current_source_dir() / 'AndroidManifest.xml.in'

setup_apk = custom_target('setup_apk',
	command: [setupsh,
		arch,
		bt,
		platform,
		manifest,
		'@OUTDIR@',
	],
	output: ['AndroidManifest.xml.in', 'base.swa.apk'],
	# NOTE: kinda redundant, pass them as args?
	# all deps listed here but swa will be relinked when
	# any other dependency changes so that shouldn't be a problem
	depends: swa_lib,
	depend_files: [manifest],
)

foreach name, mod : apks
	pname = name + '_apk'
	apk = custom_target(pname,
		command: [buildsh,
			arch,
			bt,
			platform,
			# '@INPUT@',
			mod.full_path(),
			'@OUTDIR@',
		],
		depends: [setup_apk, mod],
		output: name + '.swa.apk',
	)
	set_variable(pname, apk)
endforeach
