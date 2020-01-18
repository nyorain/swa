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

dep_vulkan_dis = dep_vulkan
if not dep_vulkan.found()
	dep_vulkan_dis = disabler()
endif

ex_vulkan = build_target('example-vulkan',
	'example-vulkan.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_vulkan_dis])

ex_vulkan_loop = build_target('example-vulkan-loop',
	'example-vulkan-loop.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_vulkan_dis])

if with_android
	dep_gl = cc.find_library('GLESv2',
		disabler: true,
		required: false)
else
	dep_gl = dependency('gl',
		disabler: true,
		required: false)
endif

ex_gl = build_target('example-gl',
	'example-gl.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_dlg, dep_gl])

dep_threads = dependency('threads', required: false)
ex_wakeup = build_target('example-wakeup',
	'example-wakeup.c',
	target_type: exe_type,
	dependencies: [swa_dep, dep_threads, dep_dlg])


if not with_android
	executable('example-exchange',
		'example-exchange.c',
		dependencies: [swa_dep, dep_dlg])
endif
