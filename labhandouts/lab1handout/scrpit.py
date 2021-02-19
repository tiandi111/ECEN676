import os
import matplotlib.pyplot as plt

full_reg_dir = 'results_full_reg'
partial_reg_dir = 'results_partial_reg'


if __name__ == '__main__':
    full_reg_files = [os.path.join(full_reg_dir, f) for f in os.listdir(full_reg_dir)]
    par_reg_files = [os.path.join(partial_reg_dir, f) for f in os.listdir(partial_reg_dir)]

    plt.rc('font', size=30)
    fig, axs = plt.subplots(len(full_reg_files), 1, figsize=(35, 35))

    for i, file_pair in enumerate(zip(full_reg_files, par_reg_files)):
        with open(file_pair[0]) as f:
            f_data = f.read().split(',')
        with open(file_pair[1]) as f:
            p_data = f.read().split(',')
        axs[i].plot([int(n) for n in f_data if n != ''], color='red', label='full register')
        axs[i].plot([int(n) for n in p_data if n != ''], color='blue', label='partial register')
        axs[i].set(xlabel='dependency distance', ylabel='count', title=os.path.split(file_pair[0])[1].split('_')[0])
        axs[i].legend()

    fig.tight_layout()
    fig.savefig('plot')
