#!/usr/local/bin/python 
#
# Author: Dong H. Ahn
#
#

import os
import os.path
import string

PerfList = []


#######################################################
# Opening/Parsing for perf metrics 
#
#
def open_wclock (d, dic):
    fn = d + "/perf-wallclock.out"    
    fo = open(fn, 'r')
    lines = fo.readlines()
    fo.close()

    di = { "DN" : fn, 
           "Prodmax" : 0,
           "Prodmin" : 0,
           #"Commmax" : 0,
           #"Commmin" : 0,
           "Syncmax" : 0,
           "Syncmin" : 0,
           "Consmax" : 0,
           "Consmin" : 0,
           "Totmax"  : 0,
           "Totmin"  : 0 }

    for l in lines:
        t = l.split(":")
        if len(t) < 2:
            continue
        tt=t[1].split(" ") 
        rc = string.find(t[0], "Producer Phase Max") 
        if rc != -1:
            di["Prodmax"] = tt[1]
            continue   
        rc = string.find(t[0], "Producer Phase Min")
        if rc != -1:
            di["Prodmin"] = tt[1]
            continue
        rc = string.find(t[0], "Sync Phase Max")
        if rc != -1:
            di["Syncmax"] = tt[1]
            continue
        rc = string.find(t[0], "Sync Phase Min")
        if rc != -1:
            di["Syncmin"] = tt[1]
            continue
        rc = string.find(t[0], "Consumer Phase Max")
        if rc != -1:
            di["Consmax"] = tt[1]
            continue
        rc = string.find(t[0], "Consumer Phase Min")
        if rc != -1:
            di["Consmin"] = tt[1]
            continue
        rc = string.find(t[0], "Total Max")
        if rc != -1:
            di["Totmax"] = tt[1]
            continue
        rc = string.find(t[0], "Total Min")
        if rc != -1:
            di["Totmin"] = tt[1]

        dic["WClock"] = di 


#######################################################
# Opening/Parsing for perf metrics 
#
#
def open_ops(fn, key, dic):
    fo = open(fn, 'r')
    lines = fo.readlines()
    fo.close()

    di = { "DN" : fn, 
           "Max" : 0,
           "Min" : 0,
           "Mean" : 0,
           "Std" : 0,
           "TP" : 0,
           "Opcount" : 0,
           "BW" : 0,
           "MaxMean" : 0,
           "MinMean" : 0,
           "MaxStd" : 0,
           "MinStd" : 0,
           "MeanStd" : 0 }

    dic[key] = di
    for l in lines:
        t = l.split(":")
        if len(t) < 2:
            continue
        tt=t[1].split(" ") 
        rc = string.find(t[0], "Max Latency") 
        if rc != -1:
            di["Max"] = tt[1]
            continue   
        rc = string.find(t[0], "Min Latency")
        if rc != -1:
            di["Min"] = tt[1]
            continue   
        rc = string.find(t[0], "Max Mean") 
        if rc != -1:
            di["MaxMean"] = tt[1]
            continue   
        rc = string.find(t[0], "Min Mean")
        if rc != -1:
            di["MinMean"] = tt[1]
            continue   
        rc = string.find(t[0], "Mean Mean")
        if rc != -1:
            di["MeanMean"] = tt[1]
            continue   
        rc = string.find(t[0], "Mean Std")
        if rc != -1:
            di["MeanStd"] = tt[1]
            break
        rc = string.find(t[0], "Mean")
        if rc != -1:
            #print di["DN"], tt
            di["Mean"] = tt[1]
            continue   
        rc = string.find(t[0], "Std Deviation")
        if rc != -1:
            di["Std"] = tt[1]
            continue   
        rc = string.find(t[0], "Throughput")
        if rc != -1:
            di["TP"] = tt[1]
            continue   
        rc = string.find(t[0], "Total OP count")
        if rc != -1:
            di["Opcount"] = tt[1].split("\n")[0]
            continue   
        rc = string.find(t[0], "Bandwidth")
        if rc != -1:
            di["BW"] = tt[1]
            continue   
        rc = string.find(t[0], "Max Std") 
        if rc != -1:
            di["MaxStd"] = tt[1]
            continue   
        rc = string.find(t[0], "Min Std")
        if rc != -1:
            di["MinStd"] = tt[1]
            continue   


#######################################################
# Parsing for files from one allocation
#
#
def parse_an_alloc (nodedir, tdic):
    dl = os.listdir(nodedir)
    for d in dl:
        T=0
        C=0
        P=0
        A=0
        V=0
        mydic = { "T" : 0, 
              "P" : 0,
              "C" : 0,
              "V" : 0,
              "A" : 0,
              "WClock" : 0,
              "Puts" : 0,
              "Commits" : 0,
              "Sync" : 0,
              "Gets" : 0 }

        tokens = d.split(":")
        for t in tokens:
            tt = t.split(".")
            mydic[tt[0]] = tt[1]
            if tt[0] == "T" :
                T = tt[1]
            elif tt[0] == "P" :
                P = tt[1]
            elif tt[0] == "C" :
                C = tt[1]
            elif tt[0] == "A" :
                A = tt[1]
            elif tt[0] == "V" :
                V = tt[1]
    
        if not T in tdic :
            tdic[T] = {}
        accdic = tdic[T]
        if not C in accdic :
            accdic[C] = {}
        pdic = accdic[C] 
        if not P in pdic :
            pdic[P] = {}
        adic = pdic[P]
        if not A in adic :
            adic[A] = {}
        vdic = adic[A]
        if not V in vdic :
            vdic[V] = {}
        valdic = vdic[V]
        valdic["VAL"] = mydic
    
        dn = nodedir + "/" + d
        open_wclock(dn, valdic["VAL"])
        open_ops(dn+"/perf-puts.out", "Puts", valdic["VAL"])
        open_ops(dn+"/perf-sync.out", "Sync", valdic["VAL"])
        open_ops(dn+"/perf-gets.out", "Gets", valdic["VAL"])


#######################################################
# Comparator for sorting in acsending order
# 
#
def com_str_int(strn1, strn2):
    return (cmp(int(strn1), int(strn2)))


#######################################################
# Comparator for sorting in descending order
# 
#
def reverse_com_str_int(strn1, strn2):
    return (cmp(int(strn2), int(strn1)))


#######################################################
# Table for one scale 
# 
#
def analyze_varying_access_count (tdic):
    #
    # ORDER: T C P A V
    # tdic[T][C][P][A][V]['VAL']
    #
    #
    print "[Analyzing the impact of the access count on performance]" 
    ttc = tdic.keys()
    ttc.sort(com_str_int)
    for cc in ttc :
        print "\n\n"
        print "[Max latencies for each of the three main phases of KAP at %s processes]" % cc
        print '%8s %8s %12s %12s %12s %12s %12s %12s %12s %12s %12s' % ("tcc", "nprods", "ncons", "nacc", "valsize", "prod max T", "prod min T", "sync max T", "sync min T", "cons max T", "cons min T")
        #print "[" + cc + "-CORES]"
        cons = tdic[cc].keys()
        cons.sort(reverse_com_str_int)
        for c in cons :
            prods = tdic[cc][c].keys()
            prods.sort(com_str_int)
            for p in prods :
                vsc = tdic[cc][c][p]['1'].keys()
                vsc.sort(com_str_int)
                for vs in vsc :
                    accc = tdic[cc][c][p].keys()
                    accc.sort(com_str_int)
                    for a in accc :
                        pmax = tdic[cc][c][p][a][vs]["VAL"]['Puts']['Max']
                        pmin = tdic[cc][c][p][a][vs]['VAL']['Puts']['Min']
                        smax = tdic[cc][c][p][a][vs]['VAL']['WClock']['Syncmax']
                        smin = tdic[cc][c][p][a][vs]['VAL']['WClock']['Syncmin']
                        cmax = tdic[cc][c][p][a][vs]['VAL']['WClock']['Consmax']
                        cmin = tdic[cc][c][p][a][vs]['VAL']['WClock']['Consmin']
                        print '%8s %8s %12s %12s %12s %12s %12s %12s %12s %12s %12s' % (cc,c,p,a,vs,pmax,pmin,smax,smin,cmax,cmin)


#######################################################
# Table for one scale 
# 
#
def analyze_varying_val_size(tdic):
    #
    # ORDER: T C P A V
    # tdic[T][C][P][A][V]['VAL']
    #
    #
    print "[Analyzing the impact of the value size on performance]" 
    ttc = tdic.keys()
    ttc.sort(reverse_com_str_int)
    for cc in ttc :
        print "\n\n"
        print "[Max latencies for each of the three main phases of KAP at %s processes]" % cc
        print '%8s %8s %12s %12s %12s %12s %12s %12s %12s %12s %12s' % ("tcc", "nprods", "ncons", "nacc", "valsize", "prod max T", "prod min T", "sync max T", "sync min T", "cons max T", "cons min T")
        #print "[" + cc + "-CORES]"
        cons = tdic[cc].keys()
        cons.sort(com_str_int)
        for c in cons :
            prods = tdic[cc][c].keys()
            prods.sort(com_str_int)
            for p in prods :
                accc = tdic[cc][c][p].keys()
                accc.sort(com_str_int)
                for a in accc :
                    vsc = tdic[cc][c][p][a].keys()
                    vsc.sort(com_str_int)
                    for vs in vsc :
                        pmax = tdic[cc][c][p][a][vs]["VAL"]['Puts']['Max']
                        pmin = tdic[cc][c][p][a][vs]['VAL']['Puts']['Min']
                        smax = tdic[cc][c][p][a][vs]['VAL']['WClock']['Syncmax']
                        smin = tdic[cc][c][p][a][vs]['VAL']['WClock']['Syncmin']
                        cmax = tdic[cc][c][p][a][vs]['VAL']['WClock']['Consmax']
                        cmin = tdic[cc][c][p][a][vs]['VAL']['WClock']['Consmin']
                        print '%8s %8s %12s %12s %12s %12s %12s %12s %12s %12s %12s' % (cc,c,p,a,vs,pmax,pmin,smax,smin,cmax,cmin)


#######################################################
# Scalability analysis for one perf metric with fixing
# access count and varying value size
#
def analyze_one_metric_vs(tdic, cate, metric):
    ttc = tdic.keys()
    ttc.sort(com_str_int)
    accc = tdic[ttc[0]][ttc[0]][ttc[0]].keys()
    accc.sort(com_str_int)

    print "[" + metric + "]"
    titlestr = '%8s %8s '
    titlelist = ["acc", "vsize"]
    for t in ttc:
        titlestr = titlestr + '%12s '
        titlelist.append(t)
    print titlestr % tuple(titlelist)

    for a in accc :
        vsc = tdic[ttc[0]][ttc[0]][ttc[0]][a].keys()
        vsc.sort(com_str_int)
        for vs in vsc :
            tmpttc = tdic.keys()
            tmpttc.sort(com_str_int)
            pstr = '%8s %8s '
            l = [a, vs]
            for t in tmpttc :
                total = t
                ncons = t
                nprod = t
                pstr = pstr + '%12s '
                l.append(tdic[t][t][t][a][vs]['VAL'][cate][metric])
            print pstr % tuple(l)


#######################################################
# Scalability analysis for one perf metric with fixing
# value size and varying access count
#
def analyze_one_metric_acc(tdic, cate, metric):
    ttc = tdic.keys()
    ttc.sort(com_str_int)
    vsc = tdic[ttc[0]][ttc[0]][ttc[0]]['1'].keys()
    vsc.sort(com_str_int)

    print "[" + metric + "]"
    titlestr = '%8s %8s '
    titlelist = ["acc", "vsize"]
    for t in ttc:
        titlestr = titlestr + '%12s '
        titlelist.append(t)
    print titlestr % tuple(titlelist)

    for vs in vsc :
        accc = tdic[ttc[0]][ttc[0]][ttc[0]].keys()
        accc.sort(com_str_int)
        for a in accc :
            tmpttc = tdic.keys()
            tmpttc.sort(com_str_int)
            pstr = '%8s %8s '
            l = [a, vs]
            for t in tmpttc :
                total = t
                ncons = t
                nprod = t
                pstr = pstr + '%12s '
                l.append(tdic[t][t][t][a][vs]['VAL'][cate][metric])
            print pstr % tuple(l)


#######################################################
# Scalability analysis for latencies for all phases
#
def analyze_scalability (tdic):
    #
    # ORDER: T C P A V
    # tdic[T][C][P][A][V]['VAL']
    #
    #

    print "\n\n"
    print "[Scalability trends with fixed access count for fully populated cases]"
    analyze_one_metric_vs(tdic, "Puts", "Max")
    analyze_one_metric_vs(tdic, "Puts", "Min")
    analyze_one_metric_vs(tdic, "WClock", "Syncmax")
    analyze_one_metric_vs(tdic, "WClock", "Syncmin")
    analyze_one_metric_vs(tdic, "WClock", "Consmax")
    analyze_one_metric_vs(tdic, "WClock", "Consmin")
    print "============================================================================================================================\n\n"

    print "\n\n"
    print "[Scalability trends with fixed value size for fully populated cases]"
    analyze_one_metric_acc(tdic, "Puts", "Max")
    analyze_one_metric_acc(tdic, "Puts", "Min")
    analyze_one_metric_acc(tdic, "WClock", "Syncmax")
    analyze_one_metric_acc(tdic, "WClock", "Syncmin")
    analyze_one_metric_acc(tdic, "WClock", "Consmax")
    analyze_one_metric_acc(tdic, "WClock", "Consmin")
    print "============================================================================================================================\n\n"


#######################################################
# Driving various analyses for info exchange patterns
#
def anal_info_exch_patterns (tdic):
    analyze_varying_val_size (tdic)
    print "============================================================================================================================\n\n"

    analyze_varying_access_count (tdic)
    print "============================================================================================================================\n\n"

    analyze_scalability (tdic)
    print "============================================================================================================================\n\n"


def main() :
    alloclist = ["0064", "0128", "0256"]
    tdic = {}
    print "============================================================================================================================"
    print "=        KAP TESTER RESULTS"
    print "=        Unit of all reported metrics: Microseconds"
    print "============================================================================================================================\n\n"

    for asize in alloclist :
        parse_an_alloc (asize, tdic)

    anal_info_exch_patterns (tdic)


#######################################################
# Entry point 
#
main()


#
# vi:tabstop=4 shiftwidth=4 expandtab
# 

