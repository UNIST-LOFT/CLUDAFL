#!/usr/bin/env python3
import os
from typing import Dict, List
import sklearn.cluster as cluster
import sklearn.metrics as metrics
import argparse
import sbsv
import pickle
import time
import sys


def read_result(filename: str) -> dict:
    parser = sbsv.parser()
    parser.add_schema("[seed] [file: str] [hash: int] [dfg: int] [res: int] [time: int] [vec: str]")
    with open(filename, 'r') as f:
        result=parser.load(f)
    
    vectors=dict()
    for r in result['seed']:
        file = r["file"]
        file_hash = r["hash"]
        dfg = r["dfg"]
        res = r["res"]
        time = r["time"]
        vec_str = r["vec"]
        vec = vec_str.strip().split(",")
        int_vec=[]
        for v in vec:
            if v!='':
                int_vec.append(int(v))
        vectors[file_hash]=int_vec
    
    return vectors

def save_clusters(clusters:Dict[str,int], path:str):
    if path == "":
        for name,cluster in clusters.items():
            print(f'{name} {cluster}')
            print(f'{name} {cluster}', file=sys.stderr)
        return
    with open(path,'w') as f:
        for name,cluster in clusters.items():
            f.write(f'{name} {cluster}\n')

def save_sklearn_model(model, path:str):
    with open(path,'wb') as f:
        pickle.dump(model,f)

"""
    Clusters
"""

def kmeans(vectors:Dict[str, List[int]], k:int=-1):
    """
        Create new k-means cluster model or load previous model and predict.
        If previous model not exist, generate new model, fit and predict.
        Otherwise, load previous model and predict without fitting.
        
        If `k` is not specified, select proper `k` with silhouette score from [2,10].
        Otherwise, use specified `k`.
        No affect if previous model exists.

        Args:
            vectors: Dict[str, List[int]] - Dictionary of vectors. Key: file_hash, Value: vector
            k: int - Number of clusters, default: -1
        Returns:
            Dict[str,int] - Dictionary of clusters. Key: file_hash, Value: cluster id
    """
    if os.path.exists(f'{args.workdir}/kmeans.pkl'):
        # Load previous model and predict (do not fit)
        with open(f'{args.workdir}/kmeans.pkl','rb') as f:
            kmeans:cluster.KMeans=pickle.load(f)
        res=kmeans.predict(list(vectors.values())) # res: array[int] of cluster ids
    else:
        # Generate new model, fit and predict if previous model not exist
        if k<2:
            # Select proper k with silhouette score if k is not specified
            K_RANGE=[2,3,4,5,6,7,8,9,10]
            best_k=2
            best_score=-1.

            for k in K_RANGE:
                kmeans = cluster.KMeans(n_clusters=k)
                res=kmeans.fit_predict(list(vectors.values()))
                score=metrics.silhouette_score(list(vectors.values()),res)
                if score>best_score:
                    best_score=score
                    best_k=k
            
            kmeans = cluster.KMeans(n_clusters=best_k)
        else:
            # Use specified k
            kmeans = cluster.KMeans(n_clusters=k)

        res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
        save_sklearn_model(kmeans,f'{args.workdir}/kmeans.pkl')
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

def bisecting_kmeans(vectors:Dict[str, List[int]], k:int=-1):
    """
        Create new bisecting k-means cluster model or load previous model and predict.
        Bisecting k-means is a hierarchical clustering algorithm based on k-means model.
        If previous model not exist, generate new model, fit and predict.
        Otherwise, load previous model and predict without fitting.
        
        If `k` is not specified, select proper `k` with silhouette score from [2,10].
        Otherwise, use specified `k`.
        No affect if previous model exists.

        Args:
            vectors: Dict[str, List[int]] - Dictionary of vectors. Key: file_hash, Value: vector
            k: int - Number of clusters, default: -1
        Returns:
            Dict[str,int] - Dictionary of clusters. Key: file_hash, Value: cluster id
    """
    # TODO: Add hierarchy
    if os.path.exists(f'{args.workdir}/bisecting-kmeans.pkl'):
        # Load previous model and predict (do not fit)
        with open(f'{args.workdir}/bisecting-kmeans.pkl','rb') as f:
            kmeans:cluster.BisectingKMeans=pickle.load(f)
        res=kmeans.predict(list(vectors.values())) # res: array[int] of cluster ids
    else:
        # Generate new model, fit and predict if previous model not exist
        if k<2:
            # Select proper k with silhouette score if k is not specified
            K_RANGE=[2,3,4,5,6,7,8,9,10]
            best_k=2
            best_score=-1.

            for k in K_RANGE:
                kmeans = cluster.BisectingKMeans(n_clusters=k)
                res=kmeans.fit_predict(list(vectors.values()))
                score=metrics.silhouette_score(list(vectors.values()),res)
                if score>best_score:
                    best_score=score
                    best_k=k
            
            kmeans = cluster.BisectingKMeans(n_clusters=best_k)
        else:
            # Use specified k
            kmeans = cluster.BisectingKMeans(n_clusters=k)
        res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
        save_sklearn_model(kmeans,f'{args.workdir}/bisecting-kmeans.pkl')
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

if __name__=='__main__':
    start = time.time()
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument('vector_path', action='store', type=str, help='Path to the directory containing the vectors')
    arg_parser.add_argument('cluster', action='store', type=str, help='Type of cluster',choices=['kmeans','bisecting-kmeans'])
    arg_parser.add_argument('workdir', action='store', type=str, help='Path to the working directory')
    arg_parser.add_argument('-k', '--k', action='store', type=int, help='Number of clusters', default=-1)
    arg_parser.add_argument('-o', '--output', action='store', type=str, help='Path to the output file', default="")
    args = arg_parser.parse_args()

    vectors=read_result(args.vector_path)

    if args.cluster=='kmeans':
        clusters=kmeans(vectors,args.k)
        save_clusters(clusters,args.output)
    elif args.cluster=='bisecting-kmeans':
        clusters=bisecting_kmeans(vectors,args.k)
        save_clusters(clusters,args.output)
    print(f"Clustering time: {time.time()-start}s", file=sys.stderr)