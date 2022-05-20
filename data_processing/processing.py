import yaml
import json
import os

# the library for the data-processing
BASE="../"
SK="PAYMENT" # for tpca
#SK="WRITE" # for rw

# trials, by default: 1
FIGURE5a_TARIALS=1
FIGURE5b_TARIALS=1
FIGURE6a_TARIALS=1
FIGURE6b_TARIALS=1

def median(a):
    return sorted(a)[len(a)//2]

def convert_yml_json(name):
  with open(name, 'r') as file:
      configuration = yaml.safe_load(file)
  
  with open(name+".json", 'w+') as json_file:
      json.dump(configuration, json_file)

  return json.load(open(name+".json"))

def find_the_yml(folder_name):
    if not os.path.isdir(folder_name):
        return ""

    for filename in os.listdir(folder_name):
        f = os.path.join(folder_name, filename)
        if os.path.isfile(f):
            if ".yml" in filename and ".json" not in filename:
                return folder_name + "/" + filename
    return ""

def figure5a():
    data_3_all, data_5_all=[],[]
    for i in range(FIGURE5a_TARIALS):
        data_3_t, data_5_t = figure5aTrail(i+1)
        data_3_all.append(data_3_t)
        data_5_all.append(data_5_t)

    data_3=data_3_all[0]
    data_5=data_5_all[0]
    H=len(data_3)

    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        tmp_99th=[]
        for j in range(FIGURE5a_TARIALS):
            tmp_50th.append(float(data_3_all[j][i][1]))
            tmp_tps.append(float(data_3_all[j][i][2]))
            tmp_99th.append(float(data_3_all[j][i][3]))
        data_3[i][1] = median(tmp_50th)
        data_3[i][2] = median(tmp_tps)
        data_3[i][3] = median(tmp_99th)

    H=len(data_5)
    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        tmp_99th=[]
        for j in range(FIGURE5a_TARIALS):
            tmp_50th.append(float(data_5_all[j][i][1]))
            tmp_tps.append(float(data_5_all[j][i][2]))
            tmp_99th.append(float(data_5_all[j][i][3]))
        data_5[i][1] = median(tmp_50th)
        data_5[i][2] = median(tmp_tps)
        data_5[i][3] = median(tmp_99th)
    return data_3, data_5


def figure5aTrail(t):
    # 3 replicas
    conc=[20, 40, 60, 80, 100, 130, 160, 190, 200, 220, 260, 300, 340, 380, 420]
    data_3=[] # (currency, 50th, tps, 99th)
    for i in conc:
        folder=BASE+"figure5a_"+str(t)+"/results_3_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_3.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]["99"]])
        else:
            print("No yml found in " + folder)
    
    # 5 replicas
    data_5=[]
    for i in conc:
        folder=BASE+"figure5a_"+str(t)+"/results_5_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_5.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]["99"]])
        else:
            print("No yml found in " + folder)
    return data_3, data_5


def figure5b():
    data_3_all, data_5_all=[],[]
    for i in range(FIGURE5b_TARIALS):
        data_3_t, data_5_t = figure5bTrail(i+1)
        data_3_all.append(data_3_t)
        data_5_all.append(data_5_t)

    data_3=data_3_all[0]
    data_5=data_5_all[0]
    H=len(data_3)

    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        for j in range(FIGURE5b_TARIALS):
            tmp_50th.append(float(data_3_all[j][i][1]))
            tmp_tps.append(float(data_3_all[j][i][2]))
        data_3[i][1] = median(tmp_50th)
        data_3[i][2] = median(tmp_tps)

    H=len(data_5)
    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        for j in range(FIGURE5b_TARIALS):
            tmp_50th.append(float(data_5_all[j][i][1]))
            tmp_tps.append(float(data_5_all[j][i][2]))
        data_5[i][1] = median(tmp_50th)
        data_5[i][2] = median(tmp_tps)
    return data_3, data_5


def figure5bTrail(t):
    # 3 replicas
    exp=[1, 2, 3, 4, 5, 6]
    data_3=[] # (exp, 50th, tps, all_latency)
    for i in exp:
        folder=BASE+"/figure5b_"+str(t)+"/results_3_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_3.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]])
        else:
            print("No yml found in " + folder)
    
    # 5 replicas
    data_5=[]
    for i in exp:
        folder=BASE+"/figure5b_"+str(t)+"/results_5_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_5.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]])
        else:
            print("No yml found in " + folder)

    return data_3, data_5

def figure6a():
    data_r_all = []
    for i in range(FIGURE6a_TARIALS):
        data_r_t = figure6aTrail(i+1)
        data_r_all.append(data_r_t)

    data_r=data_r_all[0]
    H=len(data_r)

    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        tmp_99th=[]
        for j in range(FIGURE6a_TARIALS):
            tmp_50th.append(float(data_r_all[j][i][1]))
            tmp_tps.append(float(data_r_all[j][i][2]))
            tmp_99th.append(float(data_r_all[j][i][3]))
        data_r[i][1] = median(tmp_50th)
        data_r[i][2] = median(tmp_tps)
        data_r[i][3] = median(tmp_99th)

    return data_r

def figure6aTrail(t):
    conc=[1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20]
    data_r=[] # (currency, 50th, tps, 99th)
    for i in conc:
        folder=BASE+"/figure6a_"+str(t)+"/results_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_r.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]["99"]])
        else:
            print("No yml found in " + folder)
    
    return data_r

def figure6b():
    data_l_all, data_f_all = [],[]
    for i in range(FIGURE6b_TARIALS):
        data_l_t, data_f_t = figure6bTrail(i+1)
        data_l_all.append(data_l_t)
        data_f_all.append(data_f_t)

    data_l=data_l_all[0]
    data_f=data_f_all[0]
    H=len(data_l)

    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        for j in range(FIGURE6b_TARIALS):
            tmp_50th.append(float(data_l_all[j][i][1]))
            tmp_tps.append(float(data_l_all[j][i][2]))
        data_l[i][1] = median(tmp_50th)
        data_l[i][2] = median(tmp_tps)

    H=len(data_f)
    for i in range(H):
        tmp_50th=[]
        tmp_tps=[]
        for j in range(FIGURE6b_TARIALS):
            tmp_50th.append(float(data_f_all[j][i][1]))
            tmp_tps.append(float(data_f_all[j][i][2]))
        data_f[i][1] = median(tmp_50th)
        data_f[i][2] = median(tmp_tps)

    return data_l, data_f

def figure6bTrail(t):
    # leader
    exp=[1, 2, 5, 6]
    data_l=[] # (exp, 50th, tps, all_latency)
    for i in exp:
        folder=BASE+"/figure6b_"+str(t)+"/results_leader_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_l.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]])
        else:
            print("No yml found in " + folder)
    
    # follower
    data_f=[]
    for i in exp:
        folder=BASE+"/figure6b_"+str(t)+"/results_follower_"+str(i)
        if find_the_yml(folder):
            data=convert_yml_json(find_the_yml(folder))
            data_f.append([i, data[SK]["all_latency"]["50"], data[SK]["tps"], data[SK]["all_latency"]])
        else:
            print("No yml found in " + folder)

    return data_l, data_f


if __name__ == "__main__":
    #print(figure5a())
    #print(figure5b())
    #print(figure6a())
    #print(figure6b())
    pass