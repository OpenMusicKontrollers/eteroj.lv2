project('netatom.lv2', 'c', default_options : [
	'buildtype=release',
	'warning_level=3',
	'werror=true',
	'b_lto=false',
	'c_std=c11'])

version = run_command('cat', 'VERSION').stdout().strip()

add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
cc = meson.get_compiler('c')

lv2_dep = dependency('lv2')
sratom_dep = dependency('sratom-0',
	static : meson.is_cross_build() and false) #FIXME
deps = [lv2_dep, sratom_dep]

if host_machine.system() == 'windows'
	deps += cc.find_library('ws2_32')
endif

c_args = []

if cc.has_argument('-Wno-error=format=')
	c_args += '-Wno-error=format='
endif

netatom_test = executable('netatom_test',
	join_paths('test', 'netatom_test.c'),
	c_args : c_args,
	dependencies : deps,
	install : false)

test('Test', netatom_test,
	args : ['1000'],
	timeout : 240)
