| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 5.47 | 19.41 | 11.60 | 3.55× faster | 5.47 | 12.08 | 11.47 | 2.21× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 5.20 | 13.77 | 10.87 | 2.65× faster | 5.26 | 11.13 | 10.89 | 2.12× faster |
| deep: 768 files, 97 dirs | all files | 2.18 | 4.01 | 2.22 | 1.84× faster | 2.80 | 5.09 | 2.26 | 1.82× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 2.08 | 2.85 | 2.13 | 1.37× faster | 2.66 | 3.09 | 2.17 | 1.16× faster |
| mixed: 1,452 files, 364 dirs | all files | 2.45 | 4.24 | 3.24 | 1.73× faster | 2.15 | 4.61 | 3.27 | 2.14× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 2.30 | 3.72 | 3.17 | 1.61× faster | 2.08 | 4.79 | 3.20 | 2.30× faster |

Median wall time of 21 warm runs, process startup included. fynd at dcce6ac; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, Intel(R) Xeon(R) 6975P-C. Shared CI runners are noisy: compare ratios, not absolute times.
