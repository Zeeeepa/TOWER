import React, { useEffect, useRef } from 'react';
import { LogEntry } from '../types';
import { Terminal as TerminalIcon } from 'lucide-react';

interface TerminalProps {
  logs: LogEntry[];
  title?: string;
  height?: string;
}

const Terminal: React.FC<TerminalProps> = ({ logs, title = "SYSTEM LOGS", height = "h-48" }) => {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  return (
    <div className={`flex flex-col bg-tower-800 border border-tower-600 rounded-lg overflow-hidden ${height}`}>
      <div className="flex items-center justify-between px-3 py-1 bg-tower-700 border-b border-tower-600">
        <div className="flex items-center gap-2 text-xs font-mono text-gray-400">
          <TerminalIcon size={12} />
          <span>{title}</span>
        </div>
        <div className="flex gap-1.5">
          <div className="w-2 h-2 rounded-full bg-red-500/50"></div>
          <div className="w-2 h-2 rounded-full bg-yellow-500/50"></div>
          <div className="w-2 h-2 rounded-full bg-green-500/50"></div>
        </div>
      </div>
      <div className="flex-1 overflow-y-auto p-3 font-mono text-xs space-y-1">
        {logs.length === 0 && <span className="text-gray-600 italic">Waiting for input stream...</span>}
        {logs.map((log) => (
          <div key={log.id} className="flex gap-2">
            <span className="text-gray-500 shrink-0">[{log.timestamp}]</span>
            <span className={`
              ${log.level === 'error' ? 'text-tower-danger' : ''}
              ${log.level === 'warn' ? 'text-tower-warn' : ''}
              ${log.level === 'info' ? 'text-tower-accent' : ''}
              ${log.level === 'vision' ? 'text-purple-400' : ''}
            `}>
              {log.level.toUpperCase()}
            </span>
            <span className="text-gray-300 break-all">{log.message}</span>
          </div>
        ))}
        <div ref={bottomRef} />
      </div>
    </div>
  );
};

export default Terminal;