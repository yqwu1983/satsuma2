#ifndef FORCE_DEBUG
#define NDEBUG
#endif


#include "GridSearch.h"



inline long long big_random( )
{
    return ( (long long)( rand( ) ) << 31 ) ^ (long long)( rand( ) );
}

GridSearch::GridSearch(int size, int blocks)
{
    m_size = size;

    //m_blocks = 24;
    m_blocks = blocks;
}



void GridSearch::SetUp(const vecDNAVector & target, const vecDNAVector & query)
{
    int i, j;

    ChunkManager tChunk(m_size, m_size/4);
    ChunkManager qChunk(m_size, 0);
    m_targetSeq.resize(target.size());
    m_querySeq.resize(query.size());


    // Target first
    std::vector<string> names;
    names.resize(target.size());
    for (i=0; i<target.size(); i++) {
        const char * p = target.Name(i).c_str();
        names[i] = &p[1];
        m_targetSeq[i].SetName(names[i], target[i].size());
    }

    vecDNAVector tmp;
    tChunk.ChunkIt(tmp, m_targetChunks, target, names);
    m_allNs.resize(tmp.size(), false);
    // Remember where the bad chunks are!!

    for (i=0; i<tmp.size(); i++) {
        if (tmp[i].size() == 0) {
            //cout << "Dismissing, all N's" << endl;
            m_allNs[i] = true;
        }
    }


    // Query
    names.resize(query.size());
    for (i=0; i<query.size(); i++) {
        const char * p = query.Name(i).c_str();
        names[i] = &p[1];
        m_querySeq[i].SetName(names[i], query[i].size());
    }

    qChunk.ChunkIt(tmp, m_queryChunks, query, names);

    // Find first and last blocks...

    j = 0;
    int first = -1;
    int last = 0;
    for (i=0; i<m_targetSeq.size(); i++) {
        const string & n = m_targetSeq[i].Name();
        first = -1;
        last = 0;
        for (j=0; j<m_targetChunks.size(); j++) {
            if (m_targetChunks[j].GetName() == n) {
                if (first == -1)
                    first = j;
                last = j;
            }
        }
        m_targetSeq[i].SetBlocks(first, last);
        //j--;
    }

    j = 0;
    first = -1;
    last = 0;
    for (i=0; i<m_querySeq.size(); i++) {
        const string & n = m_querySeq[i].Name();
        first = -1;
        last = 0;
        for (j=0; j<m_queryChunks.size(); j++) {
            if (m_queryChunks[j].GetName() == n) {
                if (first == -1)
                    first = j;
                last = j;
            }
        }
        m_querySeq[i].SetBlocks(first, last);

    }

    m_matrix.Setup(m_targetChunks.size(), m_queryChunks.size(), m_blocks, m_blocks);
    m_vertical.resize(m_queryChunks.size(), 0);
    m_horizontal.resize(m_targetChunks.size(), 0);

    m_repTrack.Setup(target.size(), query.size());

    for (i=0; i<target.size(); i++)
        m_repTrack.SetTargetSize(i, target[i].size());
    for (i=0; i<query.size(); i++)
        m_repTrack.SetQuerySize(i, query[i].size());

}

void GridSearch::ConsiderTargets(int targetID,
                                 int startTarget,
                                 int endTarget,
                                 int queryID,
                                 int startQuery,
                                 int endQuery,
                                 double ident)
{
    int targetBlock, queryBlock;


    if (m_repTrack.IsRepeat(targetID, startTarget, endTarget, queryID, startQuery, endQuery)) {
        //cout << "Repeat????" << endl;
        //return;
    }

    // This is very stupid...
    m_repTrack.SetRepeat(targetID, startTarget, endTarget, queryID, startQuery, endQuery);


    CoordsToBlocks(targetBlock,
                   queryBlock,
                   targetID,
                   (startTarget + endTarget)/2,
                   queryID,
                   (startQuery + endQuery)/2);

    //cout << "Mapped to blocks t=" << targetBlock << " q=" << queryBlock << endl;

    int x, y;
    //m_matrix.ClearCounts();

    m_matrix.BlockByAbsolute(x, y, targetBlock, queryBlock);

    //cout << "Grid coords x=" << x << " y=" << y << " (size_x=" << m_matrix.TargetBlocks();
    //cout << " size_y=" << m_matrix.QueryBlocks() << ")" << endl;

    //m_matrix.Set(x, y, SEARCH_HIT);


    const SearchCoordinates & c = m_matrix.Coordinates(x, y);
    //cout << "Translate back to blocks: " << c.TargetFirst() << "\t" << c.TargetLast();
    //cout << "\t" << c.QueryFirst() << "\t" << c.QueryLast() << endl;


    m_vertical[queryBlock] += endTarget - startTarget;
    m_horizontal[targetBlock] += endTarget - startTarget;

    int i, j;
    int plus = 0;
    if (m_matrix.Get(x, y) != SEARCH_UNKNOWN)
        plus = 1;

    for (i=x-plus; i<=x+plus; i++) {
        for (j=y-plus; j<=y+plus; j++) {
            if (i < 0 || j < 0)
                continue;
            if (i >= m_matrix.TargetBlocks() || j >= m_matrix.QueryBlocks())
                continue;

            //cout << "Adding to element " << i << " " << j << ", is " << m_matrix.Print(i, j) << endl;
            int w = (int)(100. * ident * (double)(endTarget - startTarget));
            m_matrix.AddCount(i, j, w);


        }
    }
}


void GridSearch::CollectSeeds(std::vector<GridTarget> & targetsOut, int n)
{
    if (n == 0)
        return;

    cout << "Looking for additional seeds" << endl;

    std::vector<GridTarget> targets;

    int i;

    double sum = 0.;
    double num = 0.;
    for (i=0; i<m_vertical.size(); i++) {
        if (m_vertical[i] > 0) {
            sum += (double)m_vertical[i];
            num += 1.;
        }
    }
    sum /= (num + 1.);
    cout << "Average vertical occupancy: " << sum << endl;


    int skipMax = 16;

    //cout << "Vertical size: " << m_vertical.size() << endl;

    int j;

    int lineCount = 0;
    int lineCountBig = 0;

    //TODO: PERFORMANCE this is inefficient!
    //a single pass can find all the points that are used and then look at the wholes using pairs (used limits).
    //This can be done across both dimensions (nested for's suck!)
    for (i=0; i<m_horizontal.size(); i++) {
        if (m_horizontal[i] > 0)
            continue;

        if (m_allNs[i])
            continue;


        for (j=i; j<m_horizontal.size(); j++) {
            if (m_horizontal[j] > 0 || m_allNs[j])
                break;
        }


        int len = j-i-1;

        if (len < 4)
            continue;

        lineCount++;


        if (len >= 4)
            lineCountBig++;

        int x = i + len/2;

        i = j;

        for (int y=0; y<m_vertical.size(); y++) {
            if (m_vertical[y] > 0)
                continue;

            int skip = 0;

            for (j=y; j<m_vertical.size(); j++) {
                if (m_vertical[j] > 0) {
                    skip++;
                    if (skip >= skipMax)
                        break;
                } else {
                    skip = 0;
                }
            }
            int y1 = y;
            int y2 = j-1;

            if (y2-y1 < 2)
                continue;


            targets.push_back(GridTarget(x,
                                         x,
                                         y1,
                                         y2,
                                         x,
                                         y1,
                                         y2,
                                         len,
                                         true));

            y = j;
        }
    }



    //cout << "Potential targets: " << targets.size() << endl;

    std::sort(targets.begin(),targets.end());

    cout << "Grid search: potential lines to be farmed out: " << lineCount << " big ones: "<< lineCountBig << endl;

    if (n < targets.size()) {
        //TODO: includes all search with same target, to find the best match to it <-- reference BIAS!!!!!
        int lastTarget = targets[n-1].TargetFirst();
        for (i=n; i<targets.size(); i++) {
            if (targets[i].TargetFirst() != lastTarget) {
                break;
            }
        }
        targets.resize(i);
        cout << "Requested: " << n << " retrieved: " << i << endl;
    }

    for (i=0; i<targets.size(); i++) {
        for (j=targets[i].TargetFirst(); j<=targets[i].TargetLast(); j++)
            m_horizontal[j] = 1;
        targetsOut.push_back(targets[i]);
    }


}


void GridSearch::UpdateTargetWeights(MultiMatches & matches){
    ClearTargetWeights();

    int lastY1 = -1;
    int lastY2 = -1;
    cout<<"Considering "<<matches.GetMatchCount()<<" matches..."<<endl;
    for (int i=0; i<matches.GetMatchCount(); i++) {
        const SingleMatch & m = matches.GetMatch(i);

        int x1 = m.GetStartTarget();
        int y1 = m.GetStartQuery();
        int x2 = m.GetStartTarget() + m.GetLength();
        int y2 = m.GetStartQuery() + m.GetLength();

        if (m.IsRC()) {
            int s = matches.GetQuerySize(m.GetQueryID());
            int tmp = y1;
            y1 = s - y2;
            y2 = s - tmp;
        }

        if (x1 < 0 || x2 < 0 || y1 < 0 || y2 < 0)
            continue;
        if (x2 >= matches.GetTargetSize(m.GetTargetID()) ||
            y1 >= matches.GetQuerySize(m.GetQueryID()) ||
            y2 >= matches.GetQuerySize(m.GetQueryID())) {
            continue;
        }
        ConsiderTargets(m.GetTargetID(), x1, x2, m.GetQueryID(), y1, y2, m.GetIdentity());
        lastY1 = y1;
        lastY2 = y2;
    }
    cout<<"Matches considered."<<endl;
}


int GridSearch::CollectTargets(std::vector<GridTarget> & targets, int n)
{
    int i, j;

    for (i=0; i<m_matrix.TargetBlocks(); i++) {
        for (j=0; j<m_matrix.QueryBlocks(); j++) {
            if (m_matrix.Get(i, j) != SEARCH_UNKNOWN)
                continue;


            if (m_matrix.GetCount(i, j) == 0)
                continue;


            const SearchCoordinates & c = m_matrix.Coordinates(i, j);

            targets.push_back(GridTarget(c.TargetFirst(),
                                         c.TargetLast(),
                                         c.QueryFirst(),
                                         c.QueryLast(),
                                         i, j,  m_matrix.GetCount(i, j)));
        }
    }


    std::sort(targets.begin(),targets.end());

    cout << "Potential targets: " << targets.size() << endl;

    int pTar = targets.size();

    if (n < targets.size()) {
        cout << "Downsize to " << n << endl;
        targets.resize(n);
    }

    for (i=0; i<targets.size(); i++) {
        m_matrix.Set(targets[i].X(), targets[i].Y(), SEARCH_SUBMITTED);
        if (targets[i].IsFast())
            cout << "ERROR: Target is FAST!" << endl;

        //cout << "Mark used x=" << targets[i].X() << " y=" <<  targets[i].Y() << " count=" << targets[i].GetCount();
        //cout << " [" << targets[i].TargetFirst() << "-" << targets[i].TargetLast() << ",";
        //cout << targets[i].QueryFirst() << "-" << targets[i].QueryLast() << "]" << endl;
    }
    //XXX: SEED targets disabled!
    if (targets.size() < n) {
        cout << "Adding in SEED targets..." << endl;
        CollectSeeds(targets, n-targets.size());
    }

    //for (i=0; i<targets.size(); i++) {
    //  if (targets[i].IsFast())
    //    cout << "ERROR(2): Target is FAST!" << endl;
    //}

    return pTar;
}


void GridSearch::CoordsToBlocks(int & targetBlock,
                                int & queryBlock,
                                int target,
                                int startTarget,
                                int query,
                                int startQuery)
{
    //cout << "Mapping t=" << target << " q=" << query << endl;

    int bStartTarget = m_targetSeq[target].First();
    int bStartQuery = m_querySeq[query].First();

    targetBlock = queryBlock = -1;

    // Stupid way of doing this...
    /*int i;
      for (i= m_targetSeq[target].First(); i<=m_targetSeq[target].Last(); i++) {
      if (startTarget >= m_targetChunks[i].GetStart() && startTarget < m_targetChunks[i].GetStart() + m_size) {
      targetBlock = i;
      break;
      }
      }
      for (i= m_querySeq[query].First(); i<=m_querySeq[query].Last(); i++) {
    //cout << "Search query, i=" << i << endl;
    if (startQuery >= m_queryChunks[i].GetStart() && startQuery < m_queryChunks[i].GetStart() + m_size) {
    queryBlock = i;
    break;
    }
    }*/

    // Query is straight-forward, as blocks don't overlap
    queryBlock= m_querySeq[query].First() + startQuery/m_size;

    // Target blocks overlap by m_size/4
    int off = startTarget/(m_size-m_size/4);
    targetBlock = m_targetSeq[target].First() + startTarget/(m_size-m_size/4);

    // To be bit-compatible with the previous code, we pick the lower block
    // if startTarget hits the overlapping part (but only if it is not the first
    // block. This will also prevent a seg fault/wrong block  when it goes over
    // the last block):
    if (off > 0 && startTarget - (m_size-m_size/4)*off < m_size/4)
        targetBlock--;

    //cout << "done!" << endl;
}