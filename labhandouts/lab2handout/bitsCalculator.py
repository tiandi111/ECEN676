

def TAGE(histLen, cntBits, tagBits, useBits, compIndexBitsArr):
    if not isinstance(compIndexBitsArr, list):
        raise TypeError('compIndexBitsArr must be list')
    return (cntBits + tagBits + useBits) * sum([2 ** bits for bits in compIndexBitsArr]) + histLen


def Global(patternBits, cntBits):
    return 2 ** patternBits * cntBits + patternBits

if __name__ == '__main__':
    print(Global(10, 2) + TAGE(256, 3, 10, 2, [9, 9, 9, 8, 8]))
