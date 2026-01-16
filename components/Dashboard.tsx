import React from 'react';
import { Service, QueueItem } from '../types';
import { Activity, Server, Zap, Database, AlertCircle } from 'lucide-react';
import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';

interface DashboardProps {
  services: Service[];
  queue: QueueItem[];
}

const data = [
  { name: '00:00', requests: 400 },
  { name: '04:00', requests: 300 },
  { name: '08:00', requests: 1200 },
  { name: '12:00', requests: 2400 },
  { name: '16:00', requests: 1800 },
  { name: '20:00', requests: 2000 },
  { name: '23:59', requests: 1500 },
];

const Dashboard: React.FC<DashboardProps> = ({ services, queue }) => {
  const onlineCount = services.filter(s => s.status === 'online').length;
  const analyzingCount = services.filter(s => s.status === 'analyzing').length;
  const errorCount = services.filter(s => s.status === 'offline').length;
  
  return (
    <div className="p-6 space-y-6 animate-fade-in">
      {/* KPI Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <div className="bg-tower-800 border border-tower-600 p-4 rounded-lg flex items-center justify-between">
          <div>
            <p className="text-gray-400 text-xs font-mono mb-1">ACTIVE SERVICES</p>
            <h3 className="text-2xl font-bold text-tower-accent">{onlineCount} / {services.length}</h3>
          </div>
          <div className="p-3 bg-tower-accent/10 rounded-full">
            <Server className="text-tower-accent" size={24} />
          </div>
        </div>

        <div className="bg-tower-800 border border-tower-600 p-4 rounded-lg flex items-center justify-between">
          <div>
            <p className="text-gray-400 text-xs font-mono mb-1">QUEUE DEPTH</p>
            <h3 className="text-2xl font-bold text-white">{queue.length}</h3>
          </div>
          <div className="p-3 bg-purple-500/10 rounded-full">
            <Database className="text-purple-400" size={24} />
          </div>
        </div>

        <div className="bg-tower-800 border border-tower-600 p-4 rounded-lg flex items-center justify-between">
          <div>
            <p className="text-gray-400 text-xs font-mono mb-1">TOTAL REQUESTS (24h)</p>
            <h3 className="text-2xl font-bold text-tower-success">
              {(services.reduce((acc, s) => acc + s.stats.requests24h, 0) / 1000).toFixed(1)}k
            </h3>
          </div>
          <div className="p-3 bg-tower-success/10 rounded-full">
            <Activity className="text-tower-success" size={24} />
          </div>
        </div>

        <div className="bg-tower-800 border border-tower-600 p-4 rounded-lg flex items-center justify-between">
          <div>
            <p className="text-gray-400 text-xs font-mono mb-1">SYSTEM STATUS</p>
            <h3 className={`text-2xl font-bold ${errorCount > 0 ? 'text-tower-danger' : 'text-tower-success'}`}>
              {errorCount > 0 ? 'WARNING' : 'OPTIMAL'}
            </h3>
          </div>
          <div className={`p-3 rounded-full ${errorCount > 0 ? 'bg-tower-danger/10' : 'bg-tower-success/10'}`}>
            {errorCount > 0 ? <AlertCircle className="text-tower-danger" size={24} /> : <Zap className="text-tower-success" size={24} />}
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Main Chart */}
        <div className="lg:col-span-2 bg-tower-800 border border-tower-600 p-4 rounded-lg">
          <div className="flex justify-between items-center mb-4">
             <h3 className="text-sm font-bold text-gray-200 font-mono flex items-center gap-2">
                <Activity size={16} /> REQUEST VOLUME
             </h3>
             <span className="text-xs text-tower-accent font-mono px-2 py-1 bg-tower-accent/10 rounded">LIVE</span>
          </div>
          <div className="h-64 w-full">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={data}>
                <defs>
                  <linearGradient id="colorRequests" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="#00f0ff" stopOpacity={0.3}/>
                    <stop offset="95%" stopColor="#00f0ff" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="#2a2a35" />
                <XAxis dataKey="name" stroke="#64748b" tick={{fontSize: 12}} />
                <YAxis stroke="#64748b" tick={{fontSize: 12}} />
                <Tooltip 
                  contentStyle={{backgroundColor: '#12121a', borderColor: '#2a2a35', color: '#fff'}}
                  itemStyle={{color: '#00f0ff'}}
                />
                <Area type="monotone" dataKey="requests" stroke="#00f0ff" fillOpacity={1} fill="url(#colorRequests)" />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Load Balancing / Queue */}
        <div className="bg-tower-800 border border-tower-600 p-4 rounded-lg">
           <h3 className="text-sm font-bold text-gray-200 font-mono mb-4 flex items-center gap-2">
              <Database size={16} /> ACTIVE QUEUE
           </h3>
           <div className="space-y-3">
             {queue.length === 0 ? (
               <div className="text-gray-500 text-sm text-center py-8">Queue is empty.</div>
             ) : (
               queue.slice(0, 5).map((item) => (
                 <div key={item.id} className="bg-tower-700 p-3 rounded flex justify-between items-center border-l-2 border-tower-accent">
                   <div>
                     <p className="text-xs text-gray-400 font-mono">REQ: {item.id.substring(0, 8)}...</p>
                     <p className="text-xs text-white">{item.status.toUpperCase()}</p>
                   </div>
                   <span className="text-xs font-mono text-tower-accent animate-pulse">Processing...</span>
                 </div>
               ))
             )}
             {analyzingCount > 0 && (
                <div className="bg-purple-900/20 p-3 rounded flex justify-between items-center border-l-2 border-purple-500">
                    <div>
                     <p className="text-xs text-purple-300 font-mono">AI AGENT</p>
                     <p className="text-xs text-white">Analyzing new service...</p>
                   </div>
                   <div className="h-2 w-2 bg-purple-500 rounded-full animate-ping"></div>
                </div>
             )}
           </div>
        </div>
      </div>
    </div>
  );
};

export default Dashboard;