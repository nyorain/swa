ex_simple = build_target('example',
	'example.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg])

ex_input = build_target('example-input',
	'example-input.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg])

ex_buffer = build_target('example-buffer',
	'example-buffer.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg])

dep_vulkan_dis = disabler()
if with_vk
	dep_vulkan_dis = dep_vulkan_full
endif

ex_vulkan = build_target('example-vulkan',
	'example-vulkan.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_vulkan_dis])

ex_vulkan_loop = build_target('example-vulkan-loop',
	'example-vulkan-loop.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_vulkan_dis])

dep_gl_dis = disabler()
if with_gl
	if with_android
		dep_gl_dis = cc.find_library('GLESv2',
			disabler: true,
			required: false)
	else
		dep_gl_dis = dependency('gl',
			disabler: true,
			required: false)
	endif
endif

ex_gl = build_target('example-gl',
	'example-gl.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_gl_dis])

# threading example not supported on windows atm due to msvc bullshit mainly
if host_machine.system() != 'windows'
	dep_threads = dependency('threads', required: false)
	ex_wakeup = build_target('example-wakeup',
		'example-wakeup.c',
		target_type: exe_type,
		dependencies: [swa_dep, dep_threads, dep_dlg])
endif


if not with_android
	executable('example-exchange',
		'example-exchange.c',
		dependencies: [swa_dep, dep_dlg])
endif
