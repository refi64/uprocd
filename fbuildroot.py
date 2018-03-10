from fbuild.builders.platform import guess_platform
from fbuild.builders.pkg_config import PkgConfig
from fbuild.builders.file import copy
from fbuild.builders.c import guess
from fbuild.builders import find_program

from fbuild.config.c import header_test, Test

from fbuild.target import register
from fbuild.record import Record
from fbuild.path import Path
import fbuild.db

from functools import partial
import os, re


class Module(Record): pass


def arguments(parser):
    group = parser.add_argument_group('config options')
    group.add_argument('--cc', help='Use the given C compiler')
    group.add_argument('--cflag', help='Pass the given flag to the C compiler',
                       action='append', default=[])
    group.add_argument('--use-color', help='Force C++ compiler colored output',
                       action='store_true', default=True)
    group.add_argument('--release', help='Build in release mode',
                       action='store_true', default=False)
    group.add_argument('--pkg-config', help='Use the given pkg-config executable')
    group.add_argument('--ruby', help='Use the given Ruby binary')
    group.add_argument('--mrkd', help='Use the given mrkd executable')
    group.add_argument('--destdir', help='Set the installation destdir', default='/')
    group.add_argument('--prefix', help='Set the installation prefix', default='usr')
    group.add_argument('--auto-service',
                       help='Automatically stop services before installation',
                       action='store_true', default=False)


class Judy(Test):
    Judy_h = header_test('Judy.h')


class MrkdBuilder(fbuild.db.PersistentObject):
    def __init__(self, ctx, exe=None):
        self.ctx = ctx
        self.mrkd = exe or find_program(ctx, ['mrkd'])

    @fbuild.db.cachemethod
    def convert(self, src: fbuild.db.SRC, *, index: fbuild.db.SRC, outdir, format):
        assert format in ('roff', 'html')

        src = Path(src)
        outdir = Path(outdir).addroot(self.ctx.buildroot)
        outdir.makedirs()

        ext = {'roff': '', 'html': '.html'}[format]
        dst = outdir / src.basename().replaceext(ext)

        cmd = [self.mrkd, src, dst, '-index', index, '-format', format]
        self.ctx.execute(cmd, 'mrkd', '%s -> %s' % (src, dst), color='yellow')
        self.ctx.db.add_external_dependencies_to_call(dsts=[dst])
        return dst


@fbuild.db.caches
def ruby_version(ctx, ruby_bin):
    ctx.logger.check('checking %s version' % ruby_bin)
    out, _ = ctx.execute([ruby_bin, '--version'], quieter=1)
    m = re.match(r'ruby (\d\.\d)', out.decode('utf-8'))
    if m is None:
        ctx.logger.failed()
        return None
    ctx.logger.passed(m.group(1))
    return m.group(1)


@fbuild.db.caches
def run_pkg_config(ctx, package):
    ctx.logger.check('checking for %s' % package)
    pkg = PkgConfig(ctx, package, exe=ctx.options.pkg_config)

    try:
        cflags = pkg.cflags()
        ldlibs = pkg.libs()
    except fbuild.ExecutionError:
        ctx.logger.failed()
        return None

    ctx.logger.passed(' '.join(cflags + ldlibs))
    return Record(cflags=cflags, ldlibs=ldlibs)


def print_config(ctx, rec):
    def padprint(c, msg):
        total = len(msg) + 22
        print(c * total)
        print(c * 10, msg, c * 10)
        print(c * total)

    def optprint(tag, value):
        print(tag, 'Yes.' if value else 'No.')

    print()
    padprint('*', 'Configure results')
    print()

    optprint('Release mode:', ctx.options.release)
    print('C compiler:', rec.c.static.compiler.cc.exe)
    print('C compiler flags:', ' '.join(set(rec.c.static.compiler.flags)))
    print('C linker flags:', ' '.join(set(rec.c.static.exe_linker.flags)))
    optprint('Build docs:', rec.mrkd)

    print()
    padprint('=', 'Modules')
    print()

    optprint('Python module:', rec.python3)
    optprint('Ruby module:', rec.ruby)
    print()


@fbuild.db.caches
def _configure(ctx, print_):
    platform = guess_platform(ctx)
    if 'linux' not in platform:
        raise fbuild.ConfigFailed('uprocd only runs under Linux.')

    flags = ctx.options.cflag
    posix_flags = ['-Wall', '-Werror', '-Wno-strict-prototypes', '-Wno-sign-compare']
    clang_flags = []

    if ctx.options.use_color:
        posix_flags.append('-fdiagnostics-color')
    if ctx.options.release:
        debug = False
        optimize = True
    else:
        debug = True
        optimize = False
        clang_flags.append('-fno-limit-debug-info')

    c = guess(ctx, exe=ctx.options.cc, flags=flags,
              debug=debug, optimize=optimize, platform_options=[
                ({'clang'}, {'flags+': clang_flags}),
                ({'posix'}, {'flags+': posix_flags,
                             'external_libs+': ['dl']}),
              ])

    libsystemd = run_pkg_config(ctx, 'libsystemd')
    if libsystemd is None:
        raise fbuild.ConfigFailed('libsystemd is required.')

    python3 = run_pkg_config(ctx, 'python3')

    ruby_bin = ruby = None
    try:
        ruby_bin = ctx.options.ruby or find_program(ctx, ['ruby'])
    except fbuild.ConfigFailed:
        pass
    else:
        ruby_ver = ruby_version(ctx, ruby_bin)
        if ruby_ver is not None:
            ruby = run_pkg_config(ctx, 'ruby-%s' % ruby_ver)

    try:
        mrkd = MrkdBuilder(ctx, ctx.options.mrkd)
    except fbuild.ConfigFailed:
        mrkd = None

    if not Judy(c.static).Judy_h:
        raise fbuild.ConfigFailed('Judy is required.')

    try:
        systemctl = find_program(ctx, ['systemctl'])
    except fbuild.ConfigFailed:
        systemctl = None

    rec = Record(c=c, libsystemd=libsystemd, python3=python3, ruby_bin=ruby_bin,
                 ruby=ruby, mrkd=mrkd, systemctl=systemctl)
    if print_:
        print_config(ctx, rec)
    return rec


@register()
def configure(ctx):
    print('NOTE: If you want to reconfigure, run fbuild configure --rebuild instead.')
    rec = _configure(ctx, print_=False)
    print_config(ctx, rec)


@fbuild.db.caches
def symlink(ctx, src: fbuild.db.SRC, dst) -> fbuild.db.DST:
    dst = Path(dst).addroot(ctx.buildroot)
    ctx.logger.check(' * symlink', '%s -> %s' % (src, dst), color='yellow')

    if dst.lexists():
        dst.remove()
    os.symlink(src.relpath(dst.parent), dst)

    return dst


def build_module(ctx, module, *, rec, uprocctl):
    if 'pkg' in module:
        if module.pkg is None:
            return
        else:
            kw = {'cflags': module.pkg.cflags, 'ldlibs': module.pkg.ldlibs}
    else:
        kw = {}

    root = Path('modules') / module.name

    binaries = []
    module_data = []

    lib = rec.c.shared.build_lib(module.name, Path.glob('modules/%s/*.c' % module.name),
                                 includes=['api'], **kw)

    module_data.append(copy(ctx, lib, (root / module.name).replaceext('.so')))
    for mod in [module.name] + module.others:
        path = (root / mod).replaceext('.module')
        module_data.append(copy(ctx, path, ctx.buildroot / path))

    for file in module.files:
        path = root / file
        module_data.append(copy(ctx, path, ctx.buildroot / path))

    for link in module.links:
        binaries.append(symlink(ctx, uprocctl, link))

    return Record(binaries=binaries, data=module_data)


def build_docs(ctx, src, *, rec, index):
    outdirs = {'roff': 'man', 'html': 'web'}
    man, html = ctx.scheduler.map(lambda fm: rec.mrkd.convert(src=src, index=index,
                                                outdir=outdirs[fm], format=fm),
                                  ['roff', 'html'])
    return Record(man=man, html=html)


def build(ctx):
    rec = _configure(ctx, print_=True)
    ctx.install_destdir = ctx.options.destdir
    ctx.install_prefix = ctx.options.prefix

    sds = rec.c.static.build_lib('sds', ['sds/sds.c'])

    common_kw = dict(
        includes=['api', 'sds', 'src/common'],
        cflags=['-fvisibility=hidden'] + rec.libsystemd.cflags,
        ldlibs=['-Wl,--export-dynamic'] + rec.libsystemd.ldlibs,
        external_libs=['Judy'],
        libs=[sds],
    )

    common = rec.c.static.build_lib('common', Path.glob('src/common/*.c'), **common_kw)

    # Avoid mutating the dict to ensure accurate rebuilds.
    common_kw = common_kw.copy()
    common_kw['libs'] = common_kw['libs'] + [common]

    cgrmvd = rec.c.static.build_exe('cgrmvd', Path.glob('src/cgrmvd/*.c'), **common_kw)
    uprocd = rec.c.static.build_exe('uprocd', Path.glob('src/uprocd/*.c'), **common_kw)
    uprocctl = rec.c.static.build_exe('uprocctl', Path.glob('src/uprocctl/*.c'),
                                      **common_kw)
    u = symlink(ctx, uprocctl, 'u')

    modules = [
        Module(name='python', pkg=rec.python3, sources='python.c',
               others=['ipython', 'mrkd', 'mypy'], files=['_uprocd_modules.py'],
               links=['upython', 'uipython', 'umrkd', 'umypy']),
        Module(name='ruby', pkg=rec.ruby, sources='ruby.c', others=[],
               files=['_uprocd_requires.rb'], links=['uruby']),
    ]

    module_outputs = ctx.scheduler.map(
                        partial(build_module, ctx, rec=rec, uprocctl=uprocctl),
                        modules)

    if rec.mrkd is None:
        return

    u_man = copy(ctx, 'man/uprocctl.1.md', 'md/u.1.md')
    page_out = ctx.scheduler.map(partial(build_docs, ctx, rec=rec,
                                         index='man/index.ini'),
                                 [u_man] + Path.glob('man/*.md'))
    u_man_out = page_out[0]

    copy(ctx, 'web/index.html', 'web')

    ctx.install(cgrmvd, 'share/uprocd/bin')
    ctx.install(uprocd, 'share/uprocd/bin')
    ctx.install(uprocctl, 'bin')
    ctx.install(u, 'bin')

    ctx.install('misc/uprocd@.service', 'lib/systemd/user')
    ctx.install('misc/cgrmvd.service', 'lib/systemd/system')
    ctx.install('misc/uprocd.policy', 'share/cgrmvd/policies')
    ctx.install('misc/com.refi64.uprocd.Cgrmvd.conf', '/etc/dbus-1/system.d')

    for i, output in enumerate(module_outputs):
        if output is None:
            man = '%s.module' % modules[i].name
            page_out = [page for page in page_out \
                        if page.man.basename().replaceext('') != man]
            continue

        for bin in output.binaries:
            ctx.install(bin, 'bin')
            page_out.append(Record(man=u_man_out.man, rename='%s.1' % bin.basename()))
        for data in output.data:
            ctx.install(data, 'share/uprocd/modules')

    for page in page_out:
        rename = page.get('rename')
        section = page.man.ext.lstrip('.')

        ctx.install(page.man, 'share/man/man%s' % section, rename=rename)


def pre_install(ctx):
    if ctx.options.auto_service:
        rec = _configure(ctx, print_=False)
        ctx.execute([rec.systemctl, 'stop', 'cgrmvd'], msg1='systemctl stop',
                    msg2='cgrmvd', color='compile', ignore_error=True)
        ctx.execute([rec.systemctl, '--user', 'stop', 'uprocd.slice'],
                    msg1='systemctl stop', msg2='uprocd (user)', color='compile',
                    ignore_error=True)


def post_install(ctx):
    if ctx.options.auto_service:
        rec = _configure(ctx, print_=False)
        ctx.execute([rec.systemctl, 'daemon-reload'], msg1='systemctl',
                    msg2='daemon-reload', color='compile', ignore_error=True)
        ctx.execute([rec.systemctl, '--user', 'daemon-reload'], msg1='systemctl',
                    msg2='daemon-reload (user)', color='compile', ignore_error=True)
