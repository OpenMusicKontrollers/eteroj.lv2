project('eteroj.lv2', 'c', default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=false',
	'c_std=c11'])

cc = meson.get_compiler('c')

m_dep = cc.find_library('m')
lv2_dep = dependency('lv2', version : '>=1.14.0')

dsp_deps = [m_dep, lv2_dep]

jsmn_inc = include_directories('jsmn')
netatom_inc = include_directories('netatom.lv2')
osc_inc = include_directories('osc.lv2')
props_inc = include_directories('props.lv2')
timely_inc = include_directories('timely.lv2')
tlsf_inc = include_directories('tlsf-3.0')
varchunk_inc = include_directories('varchunk')
inc_dir = [jsmn_inc, netatom_inc, osc_inc, props_inc, timely_inc, tlsf_inc, varchunk_inc]

inst_dir = join_paths(get_option('libdir'), 'lv2', meson.project_name())

rawvers = run_command('cat', 'VERSION').stdout().strip()
version = rawvers.split('.')

conf_data = configuration_data()
conf_data.set('MAJOR_VERSION', version[0])
conf_data.set('MINOR_VERSION', version[1])
conf_data.set('MICRO_VERSION', version[2])

add_project_arguments('-D_GNU_SOURCE', language : 'c')

cp = find_program('cp')
clone = [cp, '@INPUT@', '@OUTPUT@']

c_args = ['-fvisibility=hidden',
	'-ffast-math',
	'-Wno-unused-variable',
	'-Wno-unused-function',
	'-Wno-misleading-indentation']

dsp_srcs = ['eteroj.c',
	'eteroj_cloak.c',
	'eteroj_control.c',
	'eteroj_disk.c',
	'eteroj_io.c',
	'eteroj_ninja.c',
	'eteroj_pack.c',
	'eteroj_query.c']

dsp_srcs += join_paths('jsmn', 'jsmn.c')
dsp_srcs += join_paths('tlsf-3.0', 'tlsf.c')

if host_machine.system() == 'windows'
	dsp_deps += cc.find_library('ws2_32')
	dsp_deps += cc.find_library('psapi')
	dsp_deps += cc.find_library('iphlpapi')
elif host_machine.system() == 'darwin'
	# nothing
else
	# nothing
endif

mod = shared_module('eteroj', dsp_srcs,
	c_args : c_args,
	include_directories : inc_dir,
	name_prefix : '',
	dependencies : dsp_deps,
	install : true,
	install_dir : inst_dir)

suffix = mod.full_path().strip().split('.')[-1]
conf_data.set('MODULE_SUFFIX', '.' + suffix)

configure_file(input : 'manifest.ttl.in', output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : inst_dir)

custom_target('dsp_ttl',
	input : 'eteroj.ttl',
	output : 'eteroj.ttl',
	command : clone,
	install : true,
	install_dir : inst_dir)
