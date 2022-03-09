from compushady.backends import metal

def compile(source, entry_point='main'):
    return metal.msl_compile(source, entry_point)
