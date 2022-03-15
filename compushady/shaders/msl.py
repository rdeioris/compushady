import platform
if platform.system() == 'Darwin':
    from compushady.backends import metal


def compile(source, grid, entry_point='main'):
    return metal.msl_compile(source, entry_point, grid)
