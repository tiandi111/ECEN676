import os


def parseStat(path):
    i, corr, incorr = 0, 0, 0
    with open(path) as f:
        strs = f.read().split(" ")
        for str in strs:
            try:
                num = int(str)
                if i == 0 or i == 2:
                    corr = corr + num
                if i == 1 or i == 3:
                    incorr = incorr + num
                i = i+1
            except ValueError:
                continue
    return corr / (corr + incorr)


if __name__ == '__main__':
    f = None
    for fdir in os.listdir('.'):
        if fdir.startswith('results') and os.path.isdir(fdir):
            for fname in os.listdir(fdir):
                stat = parseStat(os.path.join(fdir, fname)) * 100
                f = open("final_results", "a")
                f.write("{}: {}%\n".format(fname, stat))
    if f is not None:
        f.close()
