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
    return corr, corr+incorr, float(corr) / float(corr + incorr)


if __name__ == '__main__':
    f = open('final_results', 'w')
    for fdir in os.listdir('.'):
        if fdir.startswith('results') and os.path.isdir(fdir):
            final_corr, final_total = 0, 0 
            f.write("-----------------------------------------------------\n")
            f.write("{}\n".format(fdir))
            f.write("-----------------------------------------------------\n")
            for fname in os.listdir(fdir):
                corr, total, acc = parseStat(os.path.join(fdir, fname))
                f.write("{}: {}%\n".format(fname, acc*100))
                final_corr = final_corr + corr
                final_total = final_total + total
   	    f.write("total: {}%\n".format(100 * float(final_corr) / float(final_total)))
    f.close()
