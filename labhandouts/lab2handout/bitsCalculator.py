

def TAGE(histLen, cntBits, tagBits, useBits, compIndexBitsArr):
    if not isinstance(compIndexBitsArr, list):
        raise TypeError('compIndexBitsArr must be list')
    return (cntBits + tagBits + useBits) * sum([2 ** bits for bits in compIndexBitsArr]) + histLen


def Global(patternBits, cntBits):
    return 2 ** patternBits * cntBits + patternBits


def PAp(patternBits, bhtSize, counterBits):
    return patternBits * bhtSize + bhtSize * counterBits * (1 << patternBits)


if __name__ == '__main__':
    TAGEBits = TAGE(310, 3, 8, 1, [9, 9, 9, 9, 9])
    GlobalBits = Global(10, 2)
    PApBits = PAp(2, 1990, 3)
    print("Global: {}".format(GlobalBits))
    print("PAp: {}".format(PApBits))
    print("Global+TAGE: {}".format(GlobalBits + TAGEBits))
    print("PAp+TAGE: {}".format(PApBits + TAGEBits))

